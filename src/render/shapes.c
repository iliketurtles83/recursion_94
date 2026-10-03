#include "shapes.h"
#include "raymath.h"
#include <math.h>

void shape_cube(LineGeo *edges, LineGeo *faces, float h, LineAttr e, LineAttr f, float width)
{
    Vector3 v[8];
    for (int i = 0; i < 8; i++) v[i] = (Vector3){ (i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h };
    static const int E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (int i = 0; i < 12; i++) lg_facing_segment(edges, v[E[i][0]], v[E[i][1]], width, width, e.intensity, e.intensity, e);
    static const int F[6][4] = { {0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3} };
    static const float N[6][3] = { {0,0,-1},{0,0,1},{0,-1,0},{0,1,0},{-1,0,0},{1,0,0} };
    for (int i = 0; i < 6; i++) {
        LineAttr fa = f;
        fa.anim = (float)i;
        lg_fill_quad(faces, v[F[i][0]], v[F[i][1]], v[F[i][2]], v[F[i][3]], (Vector3){ N[i][0], N[i][1], N[i][2] }, fa);
    }
}

void shape_octa(LineGeo *edges, LineGeo *faces, float h, LineAttr e, LineAttr f, float width)
{
    Vector3 v[6] = { { h, 0, 0 }, { -h, 0, 0 }, { 0, h, 0 }, { 0, -h, 0 }, { 0, 0, h }, { 0, 0, -h } };
    static const int E[12][2] = { {0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{4,3},{3,5},{5,2} };
    for (int i = 0; i < 12; i++) lg_facing_segment(edges, v[E[i][0]], v[E[i][1]], width, width, e.intensity, e.intensity, e);
    static const int F[8][3] = { {0,2,4},{0,4,3},{0,3,5},{0,5,2},{1,4,2},{1,3,4},{1,5,3},{1,2,5} };
    for (int i = 0; i < 8; i++) {
        Vector3 a = v[F[i][0]], b = v[F[i][1]], c = v[F[i][2]];
        Vector3 n = Vector3Normalize((Vector3){ a.x + b.x + c.x, a.y + b.y + c.y, a.z + b.z + c.z });
        LineAttr fa = f;
        fa.anim = (float)i;
        lg_fill_tri(faces, a, b, c, n, fa);
    }
}

void shape_sphere(LineGeo *edges, float r, LineAttr e, float width)
{
    const int segs = 20;
    for (int ring = 0; ring < 3; ring++) {
        Vector3 prev = { 0 };
        for (int i = 0; i <= segs; i++) {
            float a = 6.2831853f * (float)i / (float)segs, x = cosf(a) * r, y = sinf(a) * r;
            Vector3 q = ring == 0 ? (Vector3){ x, y, 0 } : ring == 1 ? (Vector3){ 0, x, y } : (Vector3){ x, 0, y };
            if (i > 0) lg_facing_segment(edges, prev, q, width, width, e.intensity, e.intensity, e);
            prev = q;
        }
    }
}
