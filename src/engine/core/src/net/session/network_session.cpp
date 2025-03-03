#include "halley/net/session/network_session.h"

#include <cassert>

#include "halley/net/connection/ack_unreliable_connection_stats.h"
#include "halley/net/connection/message_queue_udp.h"
#include "halley/net/session/network_session_control_messages.h"
#include "halley/net/connection/network_service.h"
#include "halley/net/connection/network_packet.h"
#include "halley/support/logger.h"
#include "halley/utils/algorithm.h"
using namespace Halley;

NetworkSession::NetworkSession(NetworkService& service, uint32_t networkVersion, String userName, ISharedDataHandler* sharedDataHandler)
	: service(service)
	, sharedDataHandler(sharedDataHandler)
	, networkVersion(networkVersion)
	, userName(std::move(userName))
{
}

NetworkSession::~NetworkSession()
{
	if (type == NetworkSessionType::Host) {
		service.stopListening();
	}
	close();
}

void NetworkSession::host(uint16_t maxClients)
{
	Expects(type == NetworkSessionType::Undefined);

	this->maxClients = maxClients;
	type = NetworkSessionType::Host;
	sessionSharedData = makeSessionSharedData();
	hostAddress = service.startListening([=](NetworkService::Acceptor& a) { onConnection(a); });

	setMyPeerId(0);
}

void NetworkSession::join(const String& address)
{
	Expects(type == NetworkSessionType::Undefined);

	type = NetworkSessionType::Client;
	peers.emplace_back(makePeer(0, service.connect(address)));

	ControlMsgJoin msg;
	msg.networkVersion = networkVersion;
	msg.userName = userName;
	Bytes bytes = Serializer::toBytes(msg);
	doSendToPeer(peers.back(), doMakeControlPacket(NetworkSessionControlMessageType::Join, OutboundNetworkPacket(bytes)));
	
	for (auto* listener : listeners) {
		listener->onPeerConnected(0);
	}
	hostAddress = address;
}

void NetworkSession::acceptConnection(std::shared_ptr<IConnection> incoming)
{
	const auto id = allocatePeerId();
	if (!id) {
		throw Exception("Unable to allocate peer id for incoming connection.", HalleyExceptions::Network);
	}
	
	peers.emplace_back(makePeer(id.value(), std::move(incoming)));
}

void NetworkSession::close()
{
	for (auto& peer: peers) {
		disconnectPeer(peer);
	}
	peers.clear();

	myPeerId = {};
}

void NetworkSession::setMaxClients(uint16_t clients)
{
	maxClients = clients;
}

uint16_t NetworkSession::getMaxClients() const
{
	return maxClients;
}

std::optional<NetworkSession::PeerId> NetworkSession::getMyPeerId() const
{
	return myPeerId;
}

uint16_t NetworkSession::getClientCount() const
{
	if (type == NetworkSessionType::Client) {
		return static_cast<uint16_t>(sharedData.size()); // Is this correct?
	} else if (type == NetworkSessionType::Host) {
		uint16_t i = 1;
		for (const auto& peer: peers) {
			if (peer.getStatus() == ConnectionStatus::Connected) {
				++i;
			}
		}
		return i;
	} else {
		return 0;
	}
}

Vector<NetworkSession::PeerId> NetworkSession::getRemotePeers() const
{
	Vector<PeerId> result;
	for (auto& peer: peers) {
		result.push_back(peer.peerId);
	}
	return result;
}

void NetworkSession::update(Time t)
{
	service.update(t);

	// Remove dead connections
	for (auto& peer: peers) {
		if (peer.getStatus() == ConnectionStatus::Closed) {
			disconnectPeer(peer);
		}
	}
	std_ex::erase_if(peers, [] (const Peer& peer) { return !peer.alive; });
	
	// Check for data that needs to be sent
	if (type == NetworkSessionType::Host) {
		checkForOutboundStateChanges(t, {});
	}
	if (type == NetworkSessionType::Host || type == NetworkSessionType::Client) {
		if (myPeerId) {
			auto iter = sharedData.find(myPeerId.value());
			if (iter != sharedData.end()) {
				checkForOutboundStateChanges(t, myPeerId.value());
			}
		}
	}

	// Close if connection is lost
	if (type == NetworkSessionType::Client) {
		if (peers.empty()) {
			close();
		}
	}

	// Deal with incoming messages
	processReceive();

	// Actually send
	for (auto& peer: peers) {
		peer.connection->sendAll();
	}
	service.update(0.0);

	// Update stats
	for (auto& peer: peers) {
		peer.stats->update(t);
	}
}

NetworkSessionType NetworkSession::getType() const
{
	return type;
}

bool NetworkSession::hasSessionSharedData() const
{
	return !!sessionSharedData;
}

SharedData& NetworkSession::doGetMySharedData()
{
	if (type == NetworkSessionType::Undefined || !myPeerId) {
		throw Exception("Not connected.", HalleyExceptions::Network);
	}
	auto iter = sharedData.find(myPeerId.value());
	if (iter == sharedData.end()) {
		throw Exception("Not connected.", HalleyExceptions::Network);
	}
	return *iter->second;
}

SharedData& NetworkSession::doGetMutableSessionSharedData()
{
	if (type != NetworkSessionType::Host) {
		throw Exception("Only the host can modify shared session data.", HalleyExceptions::Network);
	}
	return *sessionSharedData;
}

const SharedData& NetworkSession::doGetSessionSharedData() const
{
	return *sessionSharedData;
}

const SharedData& NetworkSession::doGetClientSharedData(PeerId clientId) const
{
	const auto result = doTryGetClientSharedData(clientId);
	if (!result) {
		throw Exception("Unknown client with id: " + toString(static_cast<int>(clientId)), HalleyExceptions::Network);
	}
	return *result;
}

const SharedData* NetworkSession::doTryGetClientSharedData(PeerId clientId) const
{
	const auto iter = sharedData.find(clientId);
	if (iter == sharedData.end()) {
		return nullptr;
	}
	return iter->second.get();
}

std::unique_ptr<SharedData> NetworkSession::makeSessionSharedData()
{
	if (sharedDataHandler) {
		return sharedDataHandler->makeSessionSharedData();
	}
	return std::make_unique<SharedData>();
}

std::unique_ptr<SharedData> NetworkSession::makePeerSharedData()
{
	if (sharedDataHandler) {
		return sharedDataHandler->makePeerSharedData();
	}
	return std::make_unique<SharedData>();
}

ConnectionStatus NetworkSession::Peer::getStatus() const
{
	return connection->getStatus();
}

ConnectionStatus NetworkSession::getStatus() const
{
	if (type == NetworkSessionType::Undefined) {
		return ConnectionStatus::Undefined;
	} else if (type == NetworkSessionType::Client) {
		if (peers.empty()) {
			return ConnectionStatus::Closed;
		} else {
			if (peers[0].getStatus() == ConnectionStatus::Connected) {
				return myPeerId && sessionSharedData ? ConnectionStatus::Connected : ConnectionStatus::Connecting;
			} else {
				return peers[0].getStatus();
			}
		}
	} else if (type == NetworkSessionType::Host) {
		return ConnectionStatus::Connected;
	} else {
		throw Exception("Unknown session type.", HalleyExceptions::Network);
	}
}

OutboundNetworkPacket NetworkSession::makeOutbound(gsl::span<const gsl::byte> data, NetworkSessionMessageHeader header)
{
	auto packet = OutboundNetworkPacket(data);
	packet.addHeader(header);
	return packet;
}

void NetworkSession::sendToPeers(OutboundNetworkPacket packet, std::optional<PeerId> except)
{
	NetworkSessionMessageHeader header;
	header.type = NetworkSessionMessageType::ToAllPeers;
	header.srcPeerId = myPeerId.value();
	header.dstPeerId = 0;

	doSendToAll(makeOutbound(packet.getBytes(), header), except);
}

void NetworkSession::sendToPeer(OutboundNetworkPacket packet, PeerId peerId)
{
	NetworkSessionMessageHeader header;
	header.type = NetworkSessionMessageType::ToPeer;
	header.srcPeerId = myPeerId.value();
	header.dstPeerId = peerId;
	packet.addHeader(header);

	for (const auto& peer: peers) {
		if (peer.peerId == peerId) {
			doSendToPeer(peer, OutboundNetworkPacket(packet));
			return;
		}
	}

	// Redirect via host
	for (const auto& peer: peers) {
		if (peer.peerId == 0) {
			doSendToPeer(peer, OutboundNetworkPacket(packet));
			return;
		}
	}
	
	Logger::logError("Unable to send message to peer " + toString(static_cast<int>(peerId)) + ": id not found.");
}

void NetworkSession::doSendToAll(OutboundNetworkPacket packet, std::optional<PeerId> except)
{
	for (const auto& peer : peers) {
		if (peer.peerId != except) {
			doSendToPeer(peer, OutboundNetworkPacket(packet));
		}
	}
}

void NetworkSession::doSendToPeer(const Peer& peer, OutboundNetworkPacket packet)
{
	//peer.connection->send(IConnection::TransmissionType::Reliable, std::move(packet));
	peer.connection->enqueue(std::move(packet), 0);
}

std::optional<std::pair<NetworkSession::PeerId, InboundNetworkPacket>> NetworkSession::receive()
{
	if (!inbox.empty()) {
		auto result = std::move(inbox[0]);
		inbox.erase(inbox.begin());
		return result;
	}
	return {};
}

void NetworkSession::processReceive()
{
	InboundNetworkPacket packet;
	for (auto& peer: peers) {
		const PeerId peerId = peer.peerId;

		for (auto& packet: peer.connection->receivePackets()) {
			// Get header
			NetworkSessionMessageHeader header;
			packet.extractHeader(header);

			if (type == NetworkSessionType::Host) {
				// Broadcast to other connections
				if (header.type == NetworkSessionMessageType::ToAllPeers) {
					// Verify client id
					if (header.srcPeerId != peerId) {
						closeConnection(peerId, "Player sent an invalid srcPlayer");
					} else {
						doSendToAll(makeOutbound(packet.getBytes(), header), peerId);
						inbox.emplace_back(header.srcPeerId, std::move(packet));
					}
				} else if (header.type == NetworkSessionMessageType::Control) {
					// Receive control
					receiveControlMessage(peerId, packet);
				} else if (header.type == NetworkSessionMessageType::ToPeer) {
					if (header.dstPeerId == myPeerId) {
						inbox.emplace_back(header.srcPeerId, std::move(packet));
					} else {
						// Redirect!
						sendToPeer(makeOutbound(packet.getBytes(), header), header.dstPeerId);
					}
				} else {
					closeConnection(peerId, "Unknown session message type: " + toString(type));
				}
			}

			else if (type == NetworkSessionType::Client) {
				if (header.type == NetworkSessionMessageType::ToAllPeers) {
					inbox.emplace_back(header.srcPeerId, std::move(packet));
				} else if (header.type == NetworkSessionMessageType::ToPeer) {
					if (header.dstPeerId == myPeerId) {
						inbox.emplace_back(header.srcPeerId, std::move(packet));
					} else {
						closeConnection(peerId, "Received message bound for a different client, aborting connection.");
					}
				} else if (header.type == NetworkSessionMessageType::Control) {
					receiveControlMessage(peerId, packet);
				} else {
					closeConnection(peerId, "Invalid session message type for client: " + toString(type));
				}
			}

			else {
				throw Exception("NetworkSession in invalid state.", HalleyExceptions::Network);
			}
		}
	}
}

void NetworkSession::closeConnection(PeerId peerId, const String& reason)
{
	Logger::logError("Closing connection: " + reason);
	for (auto& p: peers) {
		if (p.peerId == peerId) {
			disconnectPeer(p);
		}
	}
}

void NetworkSession::retransmitControlMessage(PeerId peerId, gsl::span<const gsl::byte> bytes)
{
	NetworkSessionMessageHeader header;
	header.type = NetworkSessionMessageType::Control;
	header.srcPeerId = peerId; // ?
	header.dstPeerId = 0;
	doSendToAll(makeOutbound(bytes, header), peerId);
}

void NetworkSession::receiveControlMessage(PeerId peerId, InboundNetworkPacket& packet)
{
	auto origData = packet.getBytes();

	ControlMsgHeader header;
	packet.extractHeader(header);

	switch (header.type) {
	case NetworkSessionControlMessageType::Join:
		{
			ControlMsgJoin msg = Deserializer::fromBytes<ControlMsgJoin>(packet.getBytes());
			onControlMessage(peerId, msg);
		}
		break;
	case NetworkSessionControlMessageType::SetPeerId:
		{
			ControlMsgSetPeerId msg = Deserializer::fromBytes<ControlMsgSetPeerId>(packet.getBytes());
			onControlMessage(peerId, msg);
		}
		break;
	case NetworkSessionControlMessageType::SetSessionState:
		{
			ControlMsgSetSessionState msg = Deserializer::fromBytes<ControlMsgSetSessionState>(packet.getBytes());
			onControlMessage(peerId, msg);
		}
		break;
	case NetworkSessionControlMessageType::SetPeerState:
		{
			ControlMsgSetPeerState msg = Deserializer::fromBytes<ControlMsgSetPeerState>(packet.getBytes());
			onControlMessage(peerId, msg);
			retransmitControlMessage(peerId, origData);
		}
		break;
	case NetworkSessionControlMessageType::SetServerSideData:
		{
			ControlMsgSetServerSideData msg = Deserializer::fromBytes<ControlMsgSetServerSideData>(packet.getBytes());
			onControlMessage(peerId, msg);
		}
		break;
	case NetworkSessionControlMessageType::SetServerSideDataReply:
		{
			ControlMsgSetServerSideDataReply msg = Deserializer::fromBytes<ControlMsgSetServerSideDataReply>(packet.getBytes());
			onControlMessage(peerId, msg);
		}
		break;
	case NetworkSessionControlMessageType::GetServerSideData:
		{
			ControlMsgGetServerSideData msg = Deserializer::fromBytes<ControlMsgGetServerSideData>(packet.getBytes());
			onControlMessage(peerId, msg);
		}
		break;
	case NetworkSessionControlMessageType::GetServerSideDataReply:
		{
			ControlMsgGetServerSideDataReply msg = Deserializer::fromBytes<ControlMsgGetServerSideDataReply>(packet.getBytes());
			onControlMessage(peerId, msg);
		}
		break;
	default:
		closeConnection(peerId, "Invalid control packet.");
	}
}

void NetworkSession::onControlMessage(PeerId peerId, const ControlMsgJoin& msg)
{
	Logger::logDev("Join request from peer " + toString(int(peerId)));
	
	if (myPeerId != 0) {
		closeConnection(peerId, "Only host can accept join requests.");
		return;
	}

	if (msg.networkVersion != networkVersion) {
		closeConnection(peerId, "Incompatible network version.");
		return;
	}

	ControlMsgSetPeerId outMsg;
	outMsg.peerId = peerId;
	Bytes bytes = Serializer::toBytes(outMsg);
	sharedData[outMsg.peerId] = makePeerSharedData();

	const auto& peer = getPeer(peerId);
	doSendToPeer(peer, doMakeControlPacket(NetworkSessionControlMessageType::SetPeerId, OutboundNetworkPacket(bytes)));
	doSendToPeer(peer, makeUpdateSharedDataPacket({}));
	for (auto& i : sharedData) {
		doSendToPeer(peer, makeUpdateSharedDataPacket(i.first));
	}
	for (auto* listener : listeners) {
		listener->onPeerConnected(peerId);
	}
}

void NetworkSession::onControlMessage(PeerId peerId, const ControlMsgSetPeerId& msg)
{
	Logger::logDev("Received SetPeerId");
	if (peerId != 0) {
		closeConnection(peerId, "Unauthorised control message: SetPeerId");
		return;
	}
	setMyPeerId(msg.peerId);
}

void NetworkSession::onControlMessage(PeerId peerId, const ControlMsgSetPeerState& msg)
{
	if (peerId != 0 && peerId != msg.peerId) {
		closeConnection(peerId, "Unauthorised control message: SetPeerState");
		return;
	}
	auto iter = sharedData.find(msg.peerId);

	auto s = Deserializer(msg.state);
	if (iter != sharedData.end()) {
		iter->second->deserialize(s);
	} else {
		sharedData[msg.peerId] = makePeerSharedData();
		sharedData[msg.peerId]->deserialize(s);
	}
}

void NetworkSession::onControlMessage(PeerId peerId, const ControlMsgSetSessionState& msg)
{
	if (peerId != 0) {
		closeConnection(peerId, "Unauthorised control message: SetSessionState");
		return;
	}

	Logger::logDev("Updating session state");
	if (!sessionSharedData) {
		sessionSharedData = makeSessionSharedData();
	}
	auto s = Deserializer(msg.state);
	sessionSharedData->deserialize(s);
}

void NetworkSession::onControlMessage(PeerId peerId, const ControlMsgSetServerSideData& msg)
{
	auto ok = doSetServerSideData(msg.key, ConfigNode(msg.data));

	ControlMsgSetServerSideDataReply reply;
	reply.requestId = msg.requestId;
	reply.ok = ok;
	Bytes bytes = Serializer::toBytes(reply);

	doSendToPeer(getPeer(peerId), doMakeControlPacket(NetworkSessionControlMessageType::SetServerSideDataReply, OutboundNetworkPacket(bytes)));
}

void NetworkSession::onControlMessage(PeerId peerId, const ControlMsgSetServerSideDataReply& msg)
{
	if (peerId != 0) {
		closeConnection(peerId, "Unauthorised control message: ControlMsgSetServerSideDataReply");
		return;
	}

	if (const auto iter = setServerSideDataPending.find(msg.requestId); iter != setServerSideDataPending.end()) {
		auto promise = std::move(iter->second);
		setServerSideDataPending.erase(iter);
		promise.setValue(msg.ok);
	} else {
		Logger::logWarning("Unexpected SetServerSideDataReply");
	}
}

void NetworkSession::onControlMessage(PeerId peerId, const ControlMsgGetServerSideData& msg)
{
	auto result = doGetServerSideData(msg.key);

	ControlMsgGetServerSideDataReply reply;
	reply.requestId = msg.requestId;
	reply.data = std::move(result);
	Bytes bytes = Serializer::toBytes(reply);

	doSendToPeer(getPeer(peerId), doMakeControlPacket(NetworkSessionControlMessageType::GetServerSideDataReply, OutboundNetworkPacket(bytes)));
}

void NetworkSession::onControlMessage(PeerId peerId, const ControlMsgGetServerSideDataReply& msg)
{
	if (peerId != 0) {
		closeConnection(peerId, "Unauthorised control message: ControlMsgGetServerSideDataReply");
		return;
	}

	if (const auto iter = getServerSideDataPending.find(msg.requestId); iter != getServerSideDataPending.end()) {
		auto promise = std::move(iter->second);
		getServerSideDataPending.erase(iter);
		promise.setValue(ConfigNode(msg.data));
	}
	else {
		Logger::logWarning("Unexpected GetServerSideDataReply");
	}
}

void NetworkSession::setMyPeerId(PeerId id)
{
	Expects (!myPeerId);
	myPeerId = id;
	sharedData[id] = makePeerSharedData();

	for (auto* listener: listeners) {
		listener->onStartSession(id);
	}
}

void NetworkSession::addListener(IListener* listener)
{
	if (!std_ex::contains(listeners, listener)) {
		listeners.push_back(listener);
	}
}

void NetworkSession::removeListener(IListener* listener)
{
	std_ex::erase(listeners, listener);
}

void NetworkSession::setSharedDataHandler(ISharedDataHandler* handler)
{
	sharedDataHandler = handler;
}

void NetworkSession::setServerSideDataHandler(IServerSideDataHandler* handler)
{
	serverSideDataHandler = handler;
}

const String& NetworkSession::getHostAddress() const
{
	return hostAddress;
}

NetworkService& NetworkSession::getService() const
{
	return service;
}

size_t NetworkSession::getNumConnections() const
{
	return peers.size();
}

const AckUnreliableConnectionStats& NetworkSession::getConnectionStats(size_t idx) const
{
	return *peers.at(idx).stats;
}

float NetworkSession::getLatency(size_t idx) const
{
	return peers.at(idx).connection->getLatency();
}

NetworkSession::Peer& NetworkSession::getPeer(PeerId id)
{
	return *std::find_if(peers.begin(), peers.end(), [&](const Peer& peer) { return peer.peerId == id; });
}

void NetworkSession::checkForOutboundStateChanges(Time t, std::optional<PeerId> ownerId)
{
	SharedData& data = !ownerId ? *sessionSharedData : *sharedData.at(ownerId.value());
	data.update(t);
	if (data.isModified()) {
		doSendToAll(makeUpdateSharedDataPacket(ownerId), {});
		data.markUnmodified();
		data.markSent();
	}
}

OutboundNetworkPacket NetworkSession::makeUpdateSharedDataPacket(std::optional<PeerId> ownerId)
{
	SharedData& data = !ownerId ? *sessionSharedData : *sharedData.at(ownerId.value());
	if (!ownerId) {
		ControlMsgSetSessionState state;
		state.state = Serializer::toBytes(data);
		Bytes bytes = Serializer::toBytes(state);
		return doMakeControlPacket(NetworkSessionControlMessageType::SetSessionState, OutboundNetworkPacket(bytes));
	} else {
		ControlMsgSetPeerState state;
		state.peerId = ownerId.value();
		state.state = Serializer::toBytes(data);
		Bytes bytes = Serializer::toBytes(state);
		return doMakeControlPacket(NetworkSessionControlMessageType::SetPeerState, OutboundNetworkPacket(bytes));
	}
}

OutboundNetworkPacket NetworkSession::doMakeControlPacket(NetworkSessionControlMessageType msgType, OutboundNetworkPacket packet)
{
	ControlMsgHeader ctrlHeader;
	ctrlHeader.type = msgType;
	packet.addHeader(ctrlHeader);

	NetworkSessionMessageHeader header;
	header.type = NetworkSessionMessageType::Control;
	header.srcPeerId = myPeerId ? myPeerId.value() : 0;
	header.dstPeerId = 0;
	packet.addHeader(header);

	return packet;
}

void NetworkSession::onConnection(NetworkService::Acceptor& acceptor)
{
	if (getClientCount() < maxClients) { // I'm also a client!
		acceptConnection(acceptor.accept());
	} else {
		Logger::logInfo("Rejecting network session connection as we're already at max clients.");
		acceptor.reject();
	}
}

std::optional<NetworkSession::PeerId> NetworkSession::allocatePeerId() const
{
	Expects(type == NetworkSessionType::Host);

	auto avail = Vector<uint8_t>(maxClients, 1);
	avail[0] = 0;
	for (const auto& p: peers) {
		assert(avail.at(p.peerId) == 1);
		avail.at(p.peerId) = 0;
	}

	for (uint8_t i = 1; i < avail.size(); ++i) {
		if (avail[i] != 0) {
			return i;
		}
	}
	return {};
}

void NetworkSession::disconnectPeer(Peer& peer)
{
	if (peer.getStatus() != ConnectionStatus::Closed) {
		peer.connection->close();
	}
	if (peer.alive) {
		for (auto* listener : listeners) {
			listener->onPeerDisconnected(peer.peerId);
		}
		peer.alive = false;
	}
}

NetworkSession::Peer NetworkSession::makePeer(PeerId peerId, std::shared_ptr<IConnection> connection)
{
	const size_t statsCapacity = 256; // TODO
	const size_t lineSize = 64; // TODO

	auto stats = std::make_shared<AckUnreliableConnectionStats>(statsCapacity, lineSize);
	auto ackConn = std::make_shared<AckUnreliableConnection>(std::move(connection));
	ackConn->setStatsListener(stats.get());

	auto messageQueue = std::make_shared<MessageQueueUDP>(ackConn);
	messageQueue->setChannel(0, ChannelSettings(true, true));

	return Peer{ peerId, true, std::move(messageQueue), std::move(stats) };
}

Future<bool> NetworkSession::setServerSideData(String uniqueKey, ConfigNode data)
{
	Promise<bool> result;
	if (type == NetworkSessionType::Host) {
		result.setValue(doSetServerSideData(std::move(uniqueKey), std::move(data)));
		return result.getFuture();
	} else {
		const auto id = requestId++;

		ControlMsgSetServerSideData msg;
		msg.key = std::move(uniqueKey);
		msg.data = std::move(data);
		msg.requestId = id;
		Bytes bytes = Serializer::toBytes(msg);

		doSendToPeer(peers.back(), doMakeControlPacket(NetworkSessionControlMessageType::SetServerSideData, OutboundNetworkPacket(bytes)));

		auto future = result.getFuture();
		setServerSideDataPending[id] = std::move(result);
		return future;
	}
}

Future<ConfigNode> NetworkSession::retrieveServerSideData(String uniqueKey)
{
	Promise<ConfigNode> result;
	if (type == NetworkSessionType::Host) {
		result.setValue(doGetServerSideData(std::move(uniqueKey)));
		return result.getFuture();
	} else {
		const auto id = requestId++;

		ControlMsgGetServerSideData msg;
		msg.key = std::move(uniqueKey);
		msg.requestId = id;
		Bytes bytes = Serializer::toBytes(msg);

		doSendToPeer(peers.back(), doMakeControlPacket(NetworkSessionControlMessageType::GetServerSideData, OutboundNetworkPacket(bytes)));

		auto future = result.getFuture();
		getServerSideDataPending[id] = std::move(result);
		return future;
	}
}

bool NetworkSession::doSetServerSideData(String uniqueKey, ConfigNode data)
{
	if (serverSideDataHandler) {
		return serverSideDataHandler->setServerSideData(std::move(uniqueKey), std::move(data));
	}
	return false;
}

ConfigNode NetworkSession::doGetServerSideData(String uniqueKey)
{
	if (serverSideDataHandler) {
		return serverSideDataHandler->getServerSideData(std::move(uniqueKey));
	}
	return {};
}
