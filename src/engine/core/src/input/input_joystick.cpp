/*****************************************************************\
           __
          / /
		 / /                     __  __
		/ /______    _______    / / / / ________   __       __
	   / ______  \  /_____  \  / / / / / _____  | / /      / /
	  / /      | / _______| / / / / / / /____/ / / /      / /
	 / /      / / / _____  / / / / / / _______/ / /      / /
	/ /      / / / /____/ / / / / / / |______  / |______/ /
   /_/      /_/ |________/ / / / /  \_______/  \_______  /
                          /_/ /_/                     / /
			                                         / /
		       High Level Game Framework            /_/

  ---------------------------------------------------------------

  Copyright (c) 2007-2011 - Rodrigo Braz Monteiro.
  This file is subject to the terms of halley_license.txt.

\*****************************************************************/

#include <cmath>
#include "input/input_joystick.h"
using namespace Halley;


float InputJoystick::getAxis(int n)
{
	try {
		return axisAdjust(axes.at(n));
	} catch (...) {
		return 0;
	}
}

float InputJoystick::defaultAxisAdjust(float value)
{
	float absVal = std::abs(value);
	float sign = value >= 0 ? 1.0f : -1.0f;

	const float cutOff = 0.2f;
	if (absVal < cutOff) absVal = 0;
	else absVal = (absVal-cutOff) / (1.0f - cutOff);

	return absVal * sign;
}

JoystickType InputJoystick::getJoystickType() const
{
	return JoystickType::Generic;
}

size_t InputJoystick::getNumberAxes()
{
	return axes.size();
}

size_t InputJoystick::getNumberHats()
{
	return hats.size();
}

std::shared_ptr<InputDevice> InputJoystick::getHat(int n)
{
	return hats.at(n);
}

void InputJoystick::update(Time t)
{
	updateVibration(t);
}

void InputJoystick::vibrate(spInputVibration vibration)
{
	vibs.push_back(vibration);
}

void InputJoystick::stopVibrating()
{
	vibs.clear();
}

bool InputJoystick::isEnabled() const
{
	return enabled;
}

void InputJoystick::setEnabled(bool e)
{
	if (enabled != e) {
		enabled = e;
		if (enabled) {
			lastTime = std::chrono::steady_clock::now();
		}
	}
}

void InputJoystick::clearAxes()
{
	for (auto& axis: axes) {
		axis = 0;
	}
}

bool InputJoystick::isAnyButtonPressed()
{
	for (auto& hat: hats) {
		if (hat->isAnyButtonPressed()) {
			return true;
		}
	}
	return InputButtonBase::isAnyButtonPressed();
}

bool InputJoystick::isAnyButtonReleased()
{
	for (auto& hat: hats) {
		if (hat->isAnyButtonReleased()) {
			return true;
		}
	}
	return InputButtonBase::isAnyButtonReleased();
}

bool InputJoystick::isAnyButtonDown()
{
	for (auto& hat: hats) {
		if (hat->isAnyButtonDown()) {
			return true;
		}
	}
	return InputButtonBase::isAnyButtonDown();
}

void InputJoystick::setAxisAdjust(std::function<float(float)> f)
{
	axisAdjust = f;
}

void InputJoystick::setVibration(float, float)
{
}

void InputJoystick::updateVibration(Time t)
{
	/*
	auto curTime = std::chrono::steady_clock::now();
	std::chrono::duration<double> elapsed = curTime - lastTime;
	Time t = elapsed.count();
	lastTime = curTime;
	*/

	if (!isEnabled()) {
		stopVibrating();
		return;
	}
	
	float high = 0;
	float low = 0;
	
	Vector<spInputVibration> vibs2 = std::move(vibs);
	vibs.clear();
	
	for (size_t i = 0; i < vibs2.size(); i++) {
		float h = 0;
		float l = 0;
		bool result = vibs2[i]->getState(t, h, l);
		if (result) {
			vibs.push_back(vibs2[i]);
		}
		high += h;
		low += l;
	}
	setVibration(low, high);
}
