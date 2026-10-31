// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The system sound API behind platform::openOutput / openInput (WASAPI on Windows).
#include "Audio.h"

namespace platform::backend {

bool hasSystemAudio();                                 ///< false on platforms without a backend
std::vector<AudioDevice> enumerate(bool capture);
std::unique_ptr<AudioStream> openOutput(const std::string& id, int bufferFrames, RenderFn render, std::string* error);
std::unique_ptr<AudioStream> openInput(const std::string& id, CaptureFn capture, std::string* error);

}  // namespace platform::backend
