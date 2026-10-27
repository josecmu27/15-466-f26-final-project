#include "Pathfinder.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

constexpr float Lookahead = 1.0f;
constexpr float SepRadius = 1.2f;
constexpr float SepStrength = 1.5f;

glm::vec2 Pathfinder::next_waypoint(glm::vec2 self, glm::vec2 goal, std::vector< glm::vec2 > const &others) const {
	glm::vec2 dir = grid_step(self, goal) - self;
	float len = glm::length(dir);
	if (len < 1e-4f) return self;
	dir /= len;

	glm::vec2 push(0.0f);
	for (glm::vec2 other : others) {
		glm::vec2 away = self - other;
		float d = glm::length(away);
		if (d > 1e-4f && d < SepRadius) push += (away / d) * (1.0f - d / SepRadius);
	}

	glm::vec2 v = dir + push * SepStrength;
	float vl = glm::length(v);
	if (vl > 1.0f) v /= vl;

	glm::vec2 step = v * std::min(Lookahead, len);

	glm::vec2 p = self;
	if (walkable(p + glm::vec2(step.x, 0.0f))) p.x += step.x;
	if (walkable(p + glm::vec2(0.0f, step.y))) p.y += step.y;
	return p;
}

int cell_id(int c, int r, int width) {
    return r * width + c;
}

float heuristic(int c, int r, int gc, int gr) {
    return float(std::abs(c - gc) + std::abs(r - gr));
}

glm::vec2 Pathfinder::grid_step(glm::vec2 from, glm::vec2 to) const {
	int sc = 0, sr = 0, gc = 0, gr = 0;
	if (!map.world_to_cell(glm::vec3(from, 0.0f), sc, sr)) return to;
	if (!map.world_to_cell(glm::vec3(to, 0.0f), gc, gr)) return to;
	if (sc == gc && sr == gr) return to;

	int W = map.cols();
	int H = map.rows();
    int start = cell_id(sc, sr, W);
    int goal = cell_id(gc, gr, W);

    std::vector<float> G(size_t(W * H), INFINITY);
    std::vector<int> parent(size_t(W * H), -1);
    std::vector<bool> closed(size_t(W * H), false);

	std::priority_queue<std::pair<float, int>> open;
    G[start] = 0.0f;
    open.push({-heuristic(sc, sr, gc, gr), start});

	const int dc[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	const int dr[8] = {1, 1, 0, -1, -1, -1, 0, 1};

	while (!open.empty()) {
		int cur = open.top().second;
		open.pop();
        if (closed[cur]) continue;
		if (cur == goal) break;
        closed[cur] = true;
		int c = cur % W;
		int r = cur / W;
		for (int k = 0; k < 8; k++) {
			int nc = c + dc[k];
			int nr = r + dr[k];
			if (!map.is_walkable(nc, nr) && !(nc == gc && nr == gr)) continue;
			int n = cell_id(nc, nr, W);
            if (closed[n]) continue;
			bool diagonal = (dc[k] != 0 && dr[k] != 0);
            if (diagonal && (!map.is_walkable(c + dc[k], r) || !map.is_walkable(c, r + dr[k]))) continue;
            float g = G[cur] + (diagonal ? std::sqrt(2.0f) : 1.0f);
			if (g < G[n]) {
				G[n] = g;
                parent[n] = cur;
                open.push({-(g + heuristic(nc, nr, gc, gr)), n});
			}
		}
	}
	if (parent[goal] == -1) return to;

	int step = goal;
	while (parent[step] != start) step = parent[step];
	return glm::vec2(map.cell_center(step % W, step / W));
}

bool Pathfinder::walkable(glm::vec2 p) const {
	int c = 0, r = 0;
	return map.world_to_cell(glm::vec3(p, 0.0f), c, r) && map.is_walkable(c, r);
}