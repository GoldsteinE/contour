/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <terminal/pty/Pty.h>

#include <Windows.h>

namespace terminal {

/// ConPty implementation for newer Windows 10 versions.
class ConPty : public Pty
{
  public:
    explicit ConPty(crispy::Size const& windowSize);
    ~ConPty() override;

    void close() override;

    void prepareParentProcess() override;
    void prepareChildProcess() override;

    int read(char* buf, size_t size, std::chrono::milliseconds _timeout) override;
    void wakeupReader() override;
    int write(char const* buf, size_t size) override;
    crispy::Size screenSize() const noexcept override;
    void resizeScreen(crispy::Size _cells, std::optional<crispy::Size> _pixels = std::nullopt) override;

    HPCON master() const noexcept { return master_; }

  private:
    crispy::Size size_;
    HPCON master_;
    HANDLE input_;
    HANDLE output_;
};

}  // namespace terminal
