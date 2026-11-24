#include "UiDraw.hpp"

#include "GameFonts.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTau = 6.28318530718f;
}

namespace UiDraw {

void tri(Mesh &dst, glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::u8vec4 color) {
	dst.push_back(Vertex{glm::vec3(a, 0.0f), color});
	dst.push_back(Vertex{glm::vec3(b, 0.0f), color});
	dst.push_back(Vertex{glm::vec3(c, 0.0f), color});
}

void paint_polygon(Mesh &dst, std::vector< glm::vec2 > const &pts, glm::u8vec4 color) {
	if (pts.size() < 3) return;
	glm::vec2 c(0.0f);
	for (glm::vec2 p : pts) c += p;
	c /= float(pts.size());
	for (size_t i = 0; i < pts.size(); ++i) {
		tri(dst, c, pts[i], pts[(i + 1) % pts.size()], color);
	}
}

void paint_outline(Mesh &dst, std::vector< glm::vec2 > const &pts, float width, glm::u8vec4 color, bool closed) {
	size_t n = pts.size();
	if (n < 2) return;
	float r = 0.5f * width;
	size_t segs = closed ? n : n - 1;
	for (size_t i = 0; i < segs; ++i) {
		glm::vec2 a = pts[i];
		glm::vec2 b = pts[(i + 1) % n];
		glm::vec2 d = b - a;
		float len = glm::length(d);
		if (len < 1e-6f) continue;
		glm::vec2 side = glm::vec2(-d.y, d.x) / len * r;
		tri(dst, a - side, b - side, b + side, color);
		tri(dst, a - side, b + side, a + side, color);
	}
	for (size_t i = 0; i < n; ++i) {
		for (int k = 0; k < 6; ++k) {
			float a0 = float(k) / 6.0f * kTau;
			float a1 = float(k + 1) / 6.0f * kTau;
			tri(dst, pts[i], pts[i] + r * glm::vec2(std::cos(a0), std::sin(a0)), pts[i] + r * glm::vec2(std::cos(a1), std::sin(a1)), color);
		}
	}
}

std::vector< glm::vec2 > round_rect(glm::vec2 mn, glm::vec2 mx, float rad, int segs) {
	rad = std::min(rad, 0.5f * std::min(mx.x - mn.x, mx.y - mn.y));
	glm::vec2 centers[4] = {
		glm::vec2(mx.x - rad, mn.y + rad),
		glm::vec2(mx.x - rad, mx.y - rad),
		glm::vec2(mn.x + rad, mx.y - rad),
		glm::vec2(mn.x + rad, mn.y + rad),
	};
	std::vector< glm::vec2 > pts;
	for (int c = 0; c < 4; ++c) {
		float a0 = -kTau / 4.0f + float(c) * kTau / 4.0f;
		for (int i = 0; i <= segs; ++i) {
			float a = a0 + float(i) / float(segs) * kTau / 4.0f;
			pts.push_back(centers[c] + rad * glm::vec2(std::cos(a), std::sin(a)));
		}
	}
	return pts;
}

void paint_button(Mesh &dst, glm::vec2 center, glm::vec2 half, glm::vec2 lift, glm::u8vec4 fill) {
	auto body = round_rect(center - half + lift, center + half + lift, 0.05f, 4);
	auto shadow = round_rect(center - half + glm::vec2(0.0f, -0.02f), center + half + glm::vec2(0.0f, -0.02f), 0.05f, 4);
	paint_polygon(dst, shadow, kInk);
	paint_polygon(dst, body, fill);
	paint_outline(dst, body, 0.016f, kInk, true);
}

float text_width(Font const &font, std::string const &text) {
	float advance = 0.0f;
	font.layout(text, &advance);
	return advance / float(font.pixel_height);
}

void draw_bold_text(TextRenderer &renderer, Font const &font, std::string const &text, glm::vec2 center, float height, float weight, glm::u8vec4 color) {
	glm::vec2 anchor = center - glm::vec2(0.5f * text_width(font, text) * height, kFontCapCenter * height);
	float scale = height / float(font.pixel_height);
	int rings = 3;
	for (int ring = 0; ring <= rings; ++ring) {
		float r = weight * float(ring) / float(rings);
		int count = (ring == 0) ? 1 : 6 * ring;
		for (int i = 0; i < count; ++i) {
			float a = float(i) / float(count) * kTau;
			glm::vec2 at = anchor + r * glm::vec2(std::cos(a), std::sin(a));
			renderer.draw_text(font, text, glm::vec3(at, 0.0f), glm::vec3(scale, 0.0f, 0.0f), glm::vec3(0.0f, scale, 0.0f), color);
		}
	}
}

}
