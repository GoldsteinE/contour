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
#include <terminal/Image.h>

#include <algorithm>
#include <memory>

using crispy::Size;
using std::copy;
using std::min;
using std::move;
using std::shared_ptr;

namespace terminal {

Image::Data RasterizedImage::fragment(Coordinate _pos) const
{
    // TODO: respect alignment hint
    // TODO: respect resize hint

    auto const xOffset = _pos.column * static_cast<int>(cellSize_.width);
    auto const yOffset = _pos.row * static_cast<int>(cellSize_.height);
    auto const pixelOffset = Coordinate{yOffset, xOffset};

    Image::Data fragData;
    fragData.resize(static_cast<size_t>(cellSize_.width) * static_cast<size_t>(cellSize_.height) * 4); // RGBA

    assert(image_->width() >= static_cast<unsigned>(pixelOffset.column));
    auto const availableWidth = min(image_->width() - static_cast<unsigned>(pixelOffset.column), cellSize_.width);

    assert(image_->height() >= static_cast<unsigned>(pixelOffset.row));
    auto const availableHeight = min(image_->height() - static_cast<unsigned>(pixelOffset.row), cellSize_.height);

    // auto const availableSize = Size{availableWidth, availableHeight};
    // std::cout << fmt::format(
    //     "RasterizedImage.fragment({}): pixelOffset={}, cellSize={}/{}\n",
    //     _pos,
    //     pixelOffset,
    //     cellSize_,
    //     availableSize
    // );

    // auto const fitsWidth = pixelOffset.column + cellSize_.width < image_.get().width();
    // auto const fitsHeight = pixelOffset.row + cellSize_.height < image_.get().height();
    // if (!fitsWidth || !fitsHeight)
    //     std::cout << fmt::format("ImageFragment: out of bounds{}{} ({}x{}); {}\n",
    //             fitsWidth ? "" : " (width)",
    //             fitsHeight ? "" : " (height)",
    //             availableWidth,
    //             availableHeight,
    //             *this);

    // TODO: if input format is (RGB | PNG), transform to RGBA

    auto target = &fragData[0];

    // fill horizontal gap at the bottom
    for (auto y = availableHeight * cellSize_.width; y < cellSize_.height * cellSize_.width; ++y)
    {
        *target++ = defaultColor_.red();
        *target++ = defaultColor_.green();
        *target++ = defaultColor_.blue();
        *target++ = defaultColor_.alpha();
    }

    for (auto y = 0u; y < availableHeight; ++y)
    {
        auto const startOffset = static_cast<size_t>(
            (static_cast<unsigned>(pixelOffset.row) + (availableHeight - 1 - y)) * image_->width()
          + static_cast<unsigned>(pixelOffset.column)) * 4;
        auto const source = &image_->data()[startOffset];
        target = copy(source, source + availableWidth * 4, target);

        // fill vertical gap on right
        for (auto x = availableWidth; x < cellSize_.width; ++x)
        {
            *target++ = defaultColor_.red();
            *target++ = defaultColor_.green();
            *target++ = defaultColor_.blue();
            *target++ = defaultColor_.alpha();
        }
    }

    return fragData;
}

shared_ptr<Image const> ImagePool::create(ImageFormat _format, Size _size, Image::Data&& _data)
{
    // TODO: This operation should be idempotent, i.e. if that image has been created already, return a reference to that.
    images_.emplace_back(nextImageId_++, _format, move(_data), _size);
    return shared_ptr<Image>(&images_.back(),
                             [this](Image* _image) { removeImage(_image); });
}

shared_ptr<RasterizedImage const> ImagePool::rasterize(shared_ptr<Image const> _image,
                                                       ImageAlignment _alignmentPolicy,
                                                       ImageResize _resizePolicy,
                                                       RGBAColor _defaultColor,
                                                       Size _cellSpan,
                                                       Size _cellSize)
{
    rasterizedImages_.emplace_back(move(_image), _alignmentPolicy, _resizePolicy, _defaultColor, _cellSpan, _cellSize);
    return shared_ptr<RasterizedImage>(&rasterizedImages_.back(),
                                       [this](RasterizedImage* _image) { removeRasterizedImage(_image); });
}

void ImagePool::removeImage(Image* _image)
{
    if (auto i = find_if(images_.begin(),
                         images_.end(),
                         [&](Image const& p) { return &p == _image; }); i != images_.end())
    {
        onImageRemove_(_image);
        images_.erase(i);
    }
}

void ImagePool::removeRasterizedImage(RasterizedImage* _image)
{
    if (auto i = find_if(rasterizedImages_.begin(),
                         rasterizedImages_.end(),
                         [&](RasterizedImage const& p) { return &p == _image; }); i != rasterizedImages_.end())
        rasterizedImages_.erase(i);
}

void ImagePool::link(std::string const& _name, std::shared_ptr<Image const> _imageRef)
{
    namedImages_[_name] = std::move(_imageRef);
}

std::shared_ptr<Image const> ImagePool::findImageByName(std::string const& _name) const
{
    if (auto const i = namedImages_.find(_name); i != namedImages_.end())
        return i->second;

    return {};
}

void ImagePool::unlink(std::string const& _name)
{
    namedImages_.erase(_name);
}

} // end namespace
