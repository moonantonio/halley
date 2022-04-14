#include "audio_source_layers.h"

#include "audio_clip.h"
#include "../audio_engine.h"
#include "../audio_filter_resample.h"
#include "audio_object.h"
#include "audio_source_clip.h"
#include "../audio_mixer.h"
#include "../sub_objects/audio_sub_object_layers.h"

using namespace Halley;

AudioSourceLayers::AudioSourceLayers(AudioEngine& engine, AudioEmitter& emitter, Vector<std::unique_ptr<AudioSource>> layerSources, const AudioSubObjectLayers& layerConfig)
	: engine(engine)
	, emitter(emitter)
	, layerConfig(layerConfig)
{
	layers.reserve(layerSources.size());
	for (size_t i = 0; i < layerSources.size(); ++i) {
		assert(layerSources[0]->getNumberOfChannels() == layerSources[i]->getNumberOfChannels());

		layers.emplace_back(std::move(layerSources[i]), emitter, i);
		layers.back().evaluateGain(layerConfig, emitter);
	}
}

uint8_t AudioSourceLayers::getNumberOfChannels() const
{
	return layers.empty() ? 0 : layers[0].source->getNumberOfChannels();
}

bool AudioSourceLayers::getAudioData(size_t numSamples, AudioSourceData dst)
{
	auto& mixer = engine.getMixer();
	auto result = engine.getPool().getBuffers(getNumberOfChannels(), numSamples);
	auto temp = engine.getPool().getBuffers(getNumberOfChannels(), numSamples);
	bool ok = true;

	mixer.zero(result.getSpans());
	for (auto& layer: layers) {
		layer.evaluateGain(layerConfig, emitter);
		if (layer.isPlaying(layerConfig)) {
			ok = layer.source->getAudioData(numSamples, temp.getSampleSpans()) && ok;
			mixer.mixAudio(temp.getSpans(), result.getSpans(), layer.prevGain, layer.gain);
		}
	}
	mixer.copy(result.getSpans(), dst);

	return ok;
}

bool AudioSourceLayers::isReady() const
{
	return std::all_of(layers.begin(), layers.end(), [=] (const auto& ls) { return ls.source->isReady(); });
}

AudioSourceLayers::Layer::Layer(std::unique_ptr<AudioSource> source, AudioEmitter& emitter, size_t idx)
	: source(std::move(source))
	, idx(idx)
{
}

void AudioSourceLayers::Layer::evaluateGain(const AudioSubObjectLayers& layerConfig, AudioEmitter& emitter)
{
	prevGain = gain;
	gain = layerConfig.getLayerExpression(idx).evaluate(emitter);
}

bool AudioSourceLayers::Layer::isPlaying(const AudioSubObjectLayers& layerConfig) const
{
	return gain > 0.0001f || prevGain > 0.0001f || layerConfig.isLayerSynchronised(idx);
}
