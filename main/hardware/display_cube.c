#include "display_manager.h"
#include <math.h>
#include <stdlib.h>

#define CUBE_SIZE 60.0f
#define CENTER_X 160
#define CENTER_Y 120

static const float vertices[8][3] = {
    {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
    {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
};

static const int edges[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},
    {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7}
};

// Highly optimized integer-based Bresenham's Line Algorithm
static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        display_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void render_world_locked_cube(float qw, float qx, float qy, float qz) {
    float c_qw = qw, c_qx = -qx, c_qy = -qy, c_qz = -qz;
    int projected[8][2];

    for (int i = 0; i < 8; i++) {
        float vx = vertices[i][0] * CUBE_SIZE;
        float vy = vertices[i][1] * CUBE_SIZE;
        float vz = vertices[i][2] * CUBE_SIZE;

        float t2 =   c_qw*c_qx, t3 =   c_qw*c_qy, t4 =   c_qw*c_qz;
        float t5 =  -c_qx*c_qx, t6 =   c_qx*c_qy, t7 =   c_qx*c_qz;
        float t8 =  -c_qy*c_qy, t9 =   c_qy*c_qz, t10=  -c_qz*c_qz;

        float rx = 2*( (t8 + t10)*vx + (t6 -  t4)*vy + (t3 + t7)*vz ) + vx;
        float ry = 2*( (t4 +  t6)*vx + (t5 + t10)*vy + (t9 - t2)*vz ) + vy;

        projected[i][0] = CENTER_X + (int)rx;
        projected[i][1] = CENTER_Y - (int)ry; 
    }

    for (int i = 0; i < 12; i++) {
        draw_line(projected[edges[i][0]][0], projected[edges[i][0]][1], 
                  projected[edges[i][1]][0], projected[edges[i][1]][1], 0xFFFF); // 0xFFFF is White
    }
}
