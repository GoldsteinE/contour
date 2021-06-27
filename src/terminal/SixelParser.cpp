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
#include <terminal/SixelParser.h>
#include <terminal/Coordinate.h>

#include <algorithm>

using crispy::Size;
using std::clamp;
using std::fill;
using std::max;
using std::min;
using std::vector;

namespace terminal {

namespace
{
    constexpr bool isDigit(char32_t _value) noexcept
    {
        return _value >= '0' && _value <= '9';
    }

    constexpr unsigned toDigit(char32_t _value) noexcept
    {
        return static_cast<unsigned>(_value) - '0';
    }

    constexpr bool isSixel(char32_t _value) noexcept
    {
        return _value >= 63 && _value <= 126;
    }

    constexpr int8_t toSixel(char32_t _value) noexcept
    {
        return static_cast<int8_t>(static_cast<unsigned>(_value) - 63);
    }

    constexpr RGBColor rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return RGBColor{r, g, b};
    }
}

// VT 340 default color palette (https://www.vt100.net/docs/vt3xx-gp/chapter2.html#S2.4)
constexpr inline std::array<RGBColor, 16> defaultColors = {
    rgb(0,   0,  0),    //  0: black
    rgb(51, 51, 204),   //  1: blue
    rgb(204, 33, 33),   //  2: red
    rgb(51, 204, 51),   //  3: green
    rgb(204, 51, 204),  //  4: magenta
    rgb(51, 204, 204),  //  5: cyan
    rgb(204, 204, 51),  //  6: yellow
    rgb(135, 135, 135), //  7: gray 50%
    rgb(66, 66, 66),    //  8: gray 25%
    rgb(84, 84, 153),   //  9: less saturated blue
    rgb(153, 66, 66),   // 10: less saturated red
    rgb(84, 153, 84),   // 11: less saturated green
    rgb(153, 84, 153),  // 12: less saturated magenta
    rgb(84, 153, 153),  // 13: less saturated cyan
    rgb(153, 153, 84),  // 14: less saturated yellow
    rgb(204, 204, 204), // 15: gray 75%
};

// {{{ SixelColorPalette
SixelColorPalette::SixelColorPalette(size_t _size, size_t _maxSize) :
    palette_{},
    maxSize_{_maxSize}
{
    if (_size > 0)
        palette_.resize(_size);

    reset();
}

void SixelColorPalette::reset()
{
    for (size_t i = 0; i < min(static_cast<size_t>(size()), defaultColors.size()); ++i)
        palette_[i] = defaultColors[i];
}

void SixelColorPalette::setSize(size_t _newSize)
{
    palette_.resize(static_cast<size_t>(max(size_t{0}, min(_newSize, maxSize_))));
}

void SixelColorPalette::setMaxSize(size_t _value)
{
    maxSize_ = _value;
}

void SixelColorPalette::setColor(unsigned _index, RGBColor const& _color)
{
    if (_index < maxSize_)
    {
        if (_index >= size())
            setSize(_index + 1);

        if (_index >= 0 && static_cast<size_t>(_index) < palette_.size())
            palette_.at(_index) = _color;
    }
}

RGBColor SixelColorPalette::at(unsigned _index) const noexcept
{
    return palette_[_index % palette_.size()];
}
// }}}

SixelParser::SixelParser(Events& _events, OnFinalize _finalizer) :
    events_{ _events },
    finalizer_{ move(_finalizer) }
{
}

void SixelParser::parse(char32_t _value)
{
    switch (state_)
    {
        case State::Ground:
            fallback(_value);
            break;

        case State::RepeatIntroducer:
            // '!' NUMBER BYTE
            if (isDigit(_value))
                paramShiftAndAddDigit(toDigit(_value));
            else if (isSixel(_value))
            {
                auto const sixel = toSixel(_value);
                for (unsigned i = 0; i < params_[0]; ++i)
                    events_.render(sixel);
                transitionTo(State::Ground);
            }
            else
                fallback(_value);
            break;

        case State::ColorIntroducer:
            if (isDigit(_value))
            {
                paramShiftAndAddDigit(toDigit(_value));
                transitionTo(State::ColorParam);
            }
            else
                fallback(_value);
            break;

        case State::ColorParam:
            if (isDigit(_value))
                paramShiftAndAddDigit(toDigit(_value));
            else if (_value == ';')
                params_.push_back(0);
            else
                fallback(_value);
            break;

        case State::RasterSettings:
            if (isDigit(_value))
                paramShiftAndAddDigit(toDigit(_value));
            else if (_value == ';')
                params_.push_back(0);
            else
                fallback(_value);
            break;
    }
}

void SixelParser::fallback(char32_t _value)
{
    if (_value == '#')
        transitionTo(State::ColorIntroducer);
    else if (_value == '!')
        transitionTo(State::RepeatIntroducer);
    else if (_value == '"')
        transitionTo(State::RasterSettings);
    else if (_value == '$')
    {
        transitionTo(State::Ground);
        events_.rewind();
    }
    else if (_value == '-')
    {
        transitionTo(State::Ground);
        events_.newline();
    }
    else
    {
        if (state_ != State::Ground)
            transitionTo(State::Ground);

        if (isSixel(_value))
            events_.render(toSixel(_value));
    }

    // ignore any other input value
}

void SixelParser::done()
{
    transitionTo(State::Ground); // this also ensures current state's leave action is invoked

    if (finalizer_)
        finalizer_();
}

void SixelParser::paramShiftAndAddDigit(unsigned _value)
{
    auto& number = params_.back();
    number = number * 10 + _value;
}

void SixelParser::transitionTo(State _newState)
{
    leaveState();
    state_ = _newState;
    enterState();
}

void SixelParser::enterState()
{
    switch (state_)
    {
        case State::ColorIntroducer:
        case State::RepeatIntroducer:
        case State::RasterSettings:
            params_.clear();
            params_.push_back(0);
            break;

        case State::Ground:
        case State::ColorParam:
            break;
    }
}

void SixelParser::leaveState()
{
    switch (state_)
    {
        case State::Ground:
        case State::ColorIntroducer:
        case State::RepeatIntroducer:
            break;

        case State::RasterSettings:
            if (params_.size() == 4)
            {
                auto const pan = params_[0];
                auto const pad = params_[1];
                auto const xPixels = params_[2];
                auto const yPixels = params_[3];
                events_.setRaster(pan, pad, Size{xPixels, yPixels});
                state_ = State::Ground;
            }
            break;

        case State::ColorParam:
            if (params_.size() == 1)
            {
                auto const index = params_[0];
                events_.useColor(index); // TODO: move color palette into image builder (to have access to it during clear!)
            }
            else if (params_.size() == 5)
            {
                auto constexpr convertValue = [](unsigned _value) {
                    // converts a color from range 0..100 to 0..255
                    return static_cast<uint8_t>(static_cast<unsigned>((static_cast<float>(_value) * 255.0f) / 100.0f) % 256);
                };
                auto const index = params_[0];
                auto const colorSpace = params_[1] == 2 ? Colorspace::RGB : Colorspace::HSL;
                if (colorSpace == Colorspace::RGB)
                {
                    auto const p1 = convertValue(params_[2]);
                    auto const p2 = convertValue(params_[3]);
                    auto const p3 = convertValue(params_[4]);
                    auto const color = RGBColor{p1, p2, p3}; // TODO: convert HSL if requested
                    events_.setColor(index, color);
                }
            }
            break;
    }
}

void SixelParser::start()
{
    // no-op (for now)
}

void SixelParser::pass(char32_t _char)
{
    parse(_char);
}

void SixelParser::finalize()
{
    done();
}

// =================================================================================

SixelImageBuilder::SixelImageBuilder(Size const& _maxSize,
                                     unsigned _aspectVertical,
                                     unsigned _aspectHorizontal,
                                     RGBAColor _backgroundColor,
                                     std::shared_ptr<SixelColorPalette> _colorPalette) :
    maxSize_{ _maxSize },
    colors_{ std::move(_colorPalette) },
    size_{ _maxSize },
    buffer_(size_.width * size_.height * 4),
    sixelCursor_{ 0, 0 },
    currentColor_{0},
    aspectRatio_{ _aspectVertical, _aspectHorizontal }
{
    clear(_backgroundColor);
}

void SixelImageBuilder::clear(RGBAColor _fillColor)
{
    sixelCursor_ = {0, 0};

    auto p = &buffer_[0];
    for (unsigned i = 0u; i < size_.width * size_.height; ++i)
    {
        *p++ = _fillColor.red();
        *p++ = _fillColor.green();
        *p++ = _fillColor.blue();
        *p++ = _fillColor.alpha();
    }
}

RGBAColor SixelImageBuilder::at(Coordinate _coord) const noexcept
{
    auto const row = _coord.row % size_.height;
    auto const col = _coord.column % size_.width;
    auto const base = row * size_.width * 4 + col * 4;
    auto const color = &buffer_[base];
    return RGBAColor{color[0], color[1], color[2], color[3]};
}

void SixelImageBuilder::write(Coordinate const& _coord, RGBColor const& _value) noexcept
{
    if (_coord.row >= 0 && _coord.row < size_.height && _coord.column >= 0 && _coord.column < size_.width)
    {
        auto const base = _coord.row * size_.width * 4 + _coord.column * 4;
        buffer_[base + 0] = _value.red;
        buffer_[base + 1] = _value.green;
        buffer_[base + 2] = _value.blue;
        buffer_[base + 3] = 0xFF;
    }
}

void SixelImageBuilder::setColor(unsigned _index, RGBColor const& _color)
{
    colors_->setColor(_index, _color);
}

void SixelImageBuilder::useColor(unsigned _index)
{
    currentColor_ = _index % colors_->size();
}

void SixelImageBuilder::rewind()
{
    sixelCursor_.column = 0;
}

void SixelImageBuilder::newline()
{
    sixelCursor_.column = 0;

    if (sixelCursor_.row + 6 < size_.height)
        sixelCursor_.row += 6;
}

void SixelImageBuilder::setRaster(unsigned _pan, unsigned _pad, Size const& _imageSize)
{
    aspectRatio_.nominator = _pan;
    aspectRatio_.denominator = _pad;
    size_.width = clamp(_imageSize.width, 0u, maxSize_.width);
    size_.height = clamp(_imageSize.height, 0u, maxSize_.height);

    buffer_.resize(size_.width * size_.height * 4);
}

void SixelImageBuilder::render(int8_t _sixel)
{
    // TODO: respect aspect ratio!
    auto const x = sixelCursor_.column;
    if (x < size_.width)
    {
        for (unsigned i = 0; i < 6; ++i)
        {
            auto const y = sixelCursor_.row + i;
            auto const pos = Coordinate{y, x};
            auto const pin = 1 << i;
            auto const pinned = (_sixel & pin) != 0;
            if (pinned)
                write(pos, currentColor());
        }
        sixelCursor_.column++;
    }
}

}
