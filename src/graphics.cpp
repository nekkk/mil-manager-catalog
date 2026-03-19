#include "mil/graphics.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "font8x8_basic.h"

namespace mil::gfx {

namespace {

void PutPixel(Canvas& canvas, int x, int y, std::uint32_t color) {
    if (!canvas.pixels || x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) {
        return;
    }
    canvas.pixels[y * canvas.stridePixels + x] = color;
}

std::uint8_t Channel(std::uint32_t color, int shift) {
    return static_cast<std::uint8_t>((color >> shift) & 0xFF);
}

std::uint32_t LerpColor(std::uint32_t a, std::uint32_t b, float t) {
    const auto lerp = [t](std::uint8_t lhs, std::uint8_t rhs) -> std::uint8_t {
        return static_cast<std::uint8_t>(lhs + static_cast<int>((rhs - lhs) * t));
    };

    return Rgba(lerp(Channel(a, 0), Channel(b, 0)),
                lerp(Channel(a, 8), Channel(b, 8)),
                lerp(Channel(a, 16), Channel(b, 16)),
                lerp(Channel(a, 24), Channel(b, 24)));
}

char SafeAscii(char ch) {
    return (static_cast<unsigned char>(ch) < 128 && ch >= 0) ? ch : '?';
}

std::vector<std::string> WrapLines(const std::string& text, int maxWidth, int scale, int maxLines) {
    const int maxChars = std::max(1, maxWidth / std::max(1, 8 * scale));
    std::vector<std::string> lines;
    std::stringstream paragraphStream(text);
    std::string paragraph;

    while (std::getline(paragraphStream, paragraph, '\n')) {
        std::stringstream wordStream(paragraph);
        std::string word;
        std::string line;

        while (wordStream >> word) {
            std::string candidate = line.empty() ? word : line + ' ' + word;
            if (static_cast<int>(candidate.size()) <= maxChars) {
                line = std::move(candidate);
            } else {
                if (!line.empty()) {
                    lines.push_back(line);
                    if (maxLines > 0 && static_cast<int>(lines.size()) >= maxLines) {
                        return lines;
                    }
                }
                if (static_cast<int>(word.size()) <= maxChars) {
                    line = word;
                } else {
                    std::size_t offset = 0;
                    while (offset < word.size()) {
                        lines.push_back(word.substr(offset, static_cast<std::size_t>(maxChars)));
                        if (maxLines > 0 && static_cast<int>(lines.size()) >= maxLines) {
                            return lines;
                        }
                        offset += static_cast<std::size_t>(maxChars);
                    }
                    line.clear();
                }
            }
        }

        if (!line.empty()) {
            lines.push_back(line);
            if (maxLines > 0 && static_cast<int>(lines.size()) >= maxLines) {
                return lines;
            }
        } else if (paragraph.empty()) {
            lines.emplace_back();
            if (maxLines > 0 && static_cast<int>(lines.size()) >= maxLines) {
                return lines;
            }
        }
    }

    return lines;
}

void DrawGlyph(Canvas& canvas, int x, int y, char ch, std::uint32_t color, int scale) {
    const unsigned char* glyph = reinterpret_cast<const unsigned char*>(font8x8_basic[static_cast<unsigned char>(SafeAscii(ch))]);
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            if ((glyph[row] & (1 << col)) == 0) {
                continue;
            }
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    PutPixel(canvas, x + col * scale + dx, y + row * scale + dy, color);
                }
            }
        }
    }
}

}  // namespace

Canvas BeginFrame(Framebuffer& framebuffer) {
    Canvas canvas;
    if (!framebuffer.has_init) {
        return canvas;
    }

    u32 stride = 0;
    canvas.pixels = static_cast<std::uint32_t*>(framebufferBegin(&framebuffer, &stride));
    canvas.width = 1280;
    canvas.height = 720;
    canvas.stridePixels = static_cast<int>(stride / sizeof(std::uint32_t));
    return canvas;
}

void EndFrame(Framebuffer& framebuffer) {
    framebufferEnd(&framebuffer);
}

std::uint32_t Rgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
    return RGBA8(red, green, blue, alpha);
}

void Clear(Canvas& canvas, std::uint32_t color) {
    for (int y = 0; y < canvas.height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            canvas.pixels[y * canvas.stridePixels + x] = color;
        }
    }
}

void ClearVerticalGradient(Canvas& canvas, std::uint32_t topColor, std::uint32_t bottomColor) {
    for (int y = 0; y < canvas.height; ++y) {
        const float t = canvas.height <= 1 ? 0.0f : static_cast<float>(y) / static_cast<float>(canvas.height - 1);
        const std::uint32_t rowColor = LerpColor(topColor, bottomColor, t);
        for (int x = 0; x < canvas.width; ++x) {
            canvas.pixels[y * canvas.stridePixels + x] = rowColor;
        }
    }
}

void FillRect(Canvas& canvas, int x, int y, int width, int height, std::uint32_t color) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(canvas.width, x + width);
    const int y1 = std::min(canvas.height, y + height);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            canvas.pixels[yy * canvas.stridePixels + xx] = color;
        }
    }
}

void DrawRect(Canvas& canvas, int x, int y, int width, int height, std::uint32_t color) {
    FillRect(canvas, x, y, width, 1, color);
    FillRect(canvas, x, y + height - 1, width, 1, color);
    FillRect(canvas, x, y, 1, height, color);
    FillRect(canvas, x + width - 1, y, 1, height, color);
}

void DrawText(Canvas& canvas, int x, int y, const std::string& text, std::uint32_t color, int scale) {
    int cursorX = x;
    int cursorY = y;
    for (char ch : text) {
        if (ch == '\n') {
            cursorX = x;
            cursorY += LineHeight(scale);
            continue;
        }
        DrawGlyph(canvas, cursorX, cursorY, ch, color, scale);
        cursorX += 8 * scale;
    }
}

int DrawTextWrapped(Canvas& canvas,
                    int x,
                    int y,
                    int maxWidth,
                    const std::string& text,
                    std::uint32_t color,
                    int scale,
                    int maxLines) {
    const auto lines = WrapLines(text, maxWidth, scale, maxLines);
    int drawnLines = 0;
    for (const std::string& line : lines) {
        DrawText(canvas, x, y + drawnLines * LineHeight(scale), line, color, scale);
        ++drawnLines;
    }
    return drawnLines * LineHeight(scale);
}

int MeasureTextWidth(const std::string& text, int scale) {
    int width = 0;
    int lineWidth = 0;
    for (char ch : text) {
        if (ch == '\n') {
            width = std::max(width, lineWidth);
            lineWidth = 0;
            continue;
        }
        lineWidth += 8 * scale;
    }
    return std::max(width, lineWidth);
}

int LineHeight(int scale) {
    return 10 * scale;
}

}  // namespace mil::gfx
