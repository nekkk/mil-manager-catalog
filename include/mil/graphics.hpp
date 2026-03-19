#pragma once

#include <cstdint>
#include <string>

#include <switch.h>

namespace mil::gfx {

struct Canvas {
    std::uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stridePixels = 0;
};

Canvas BeginFrame(Framebuffer& framebuffer);
void EndFrame(Framebuffer& framebuffer);

std::uint32_t Rgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 0xFF);
void Clear(Canvas& canvas, std::uint32_t color);
void ClearVerticalGradient(Canvas& canvas, std::uint32_t topColor, std::uint32_t bottomColor);
void FillRect(Canvas& canvas, int x, int y, int width, int height, std::uint32_t color);
void DrawRect(Canvas& canvas, int x, int y, int width, int height, std::uint32_t color);
void DrawText(Canvas& canvas, int x, int y, const std::string& text, std::uint32_t color, int scale = 1);
int DrawTextWrapped(Canvas& canvas,
                    int x,
                    int y,
                    int maxWidth,
                    const std::string& text,
                    std::uint32_t color,
                    int scale = 1,
                    int maxLines = 0);
int MeasureTextWidth(const std::string& text, int scale = 1);
int LineHeight(int scale = 1);

}  // namespace mil::gfx
