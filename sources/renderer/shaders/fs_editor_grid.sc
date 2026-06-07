$input v_texcoord0

#include <bgfx_shader.sh>

uniform vec4 u_editorGridCameraPos;
uniform vec4 u_editorGridBasisRight;
uniform vec4 u_editorGridBasisUp;
uniform vec4 u_editorGridBasisForward;
uniform vec4 u_editorGridParams;
uniform vec4 u_editorGridOrigin;
uniform vec4 u_editorGridWidths;
uniform vec4 u_editorGridStyle;

float axis_line_coverage(float t, float spacing, float width_pixels)
{
    float safe_spacing = max(spacing, 0.0001);
    float dt = max(fwidth(t), 1e-6);
    float dq = dt / safe_spacing;
    float q = t / safe_spacing;
    float h = min(max(width_pixels, 0.0) * 0.5 * dq, 0.5);
    if (h <= 0.0) {
        return 0.0;
    }
    float line_frac = 2.0 * h;
    float a = q - 0.5 * dq + h;
    float b = q + 0.5 * dq + h;
    float integral = (floor(b) - floor(a)) * line_frac
        + min(fract(b), line_frac)
        - min(fract(a), line_frac);
    return clamp(integral / dq, 0.0, 1.0);
}

float line_mask(vec2 coord, float spacing, float width_pixels)
{
    float cx = axis_line_coverage(coord.x, spacing, width_pixels);
    float cy = axis_line_coverage(coord.y, spacing, width_pixels);
    return cx + cy - cx * cy;
}

float axis_mask(float coord, float width_pixels)
{
    float dt = max(fwidth(coord), 1e-6);
    float h = max(width_pixels, 0.0) * 0.5 * dt;
    if (h <= 0.0) {
        return 0.0;
    }
    float a = coord - 0.5 * dt;
    float b = coord + 0.5 * dt;
    float overlap = max(0.0, min(b, h) - max(a, -h));
    return clamp(overlap / dt, 0.0, 1.0);
}

float pixel_world_span(vec2 coord)
{
    vec2 derivative = max(fwidth(coord), vec2(0.0001, 0.0001));
    return max(derivative.x, derivative.y);
}

float smooth_unit(float v)
{
    float t = clamp(v, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float grid_lod(float pixel_world, float base_spacing, float ratio)
{
    float target_cell_pixels = 18.0;
    float desired = max(pixel_world * target_cell_pixels / max(base_spacing, 0.0001), 1.0);
    return clamp(log2(desired) / max(log2(ratio), 0.0001), 0.0, 8.999);
}

void main()
{
    bool is_ortho = u_editorGridBasisForward.w > 0.5;
    vec3 ray_o;
    vec3 ray;
    if (is_ortho) {
        ray_o = u_editorGridCameraPos.xyz
            + u_editorGridBasisRight.xyz * (v_texcoord0.x * u_editorGridBasisRight.w)
            - u_editorGridBasisUp.xyz * (v_texcoord0.y * u_editorGridBasisUp.w);
        ray = normalize(u_editorGridBasisForward.xyz);
    } else {
        ray_o = u_editorGridCameraPos.xyz;
        ray = normalize(
            u_editorGridBasisForward.xyz
            + u_editorGridBasisRight.xyz * (v_texcoord0.x * u_editorGridBasisRight.w)
            - u_editorGridBasisUp.xyz * (v_texcoord0.y * u_editorGridBasisUp.w)
        );
    }

    float plane_offset = u_editorGridCameraPos.w - ray_o.y;
    float fade_start = max(u_editorGridParams.z, 0.0);
    float fade_end = max(u_editorGridParams.w, fade_start + 1.0);
    float max_hit_distance = fade_end * 1.5;
    float min_abs_ry = max(abs(plane_offset) / max_hit_distance, 1e-6);
    float ry_safe = max(abs(ray.y), min_abs_ry);
    float hit_distance = abs(plane_offset) / ry_safe;

    vec3 hit = ray_o + ray * hit_distance;
    vec2 world_xz = hit.xz;
    vec2 grid_xz = world_xz - u_editorGridOrigin.xy;

    float base_spacing = max(u_editorGridParams.x, 0.0001);
    float ratio = max(u_editorGridParams.y / base_spacing, 2.0);
    float camera_distance = length(world_xz - u_editorGridCameraPos.xz);
    float far_fade = 1.0 - smoothstep(fade_start, fade_end, camera_distance);

    float lod = grid_lod(pixel_world_span(grid_xz), base_spacing, ratio);
    float lod_level = floor(lod);
    float lod_blend = smooth_unit(lod - lod_level);

    float spacing_0 = base_spacing * pow(ratio, lod_level);
    float spacing_1 = spacing_0 * ratio;
    float spacing_2 = spacing_1 * ratio;

    float lvl_0 = line_mask(grid_xz, spacing_0, u_editorGridWidths.x);
    float lvl_1 = line_mask(grid_xz, spacing_1, u_editorGridWidths.y);
    float lvl_2 = line_mask(grid_xz, spacing_2, u_editorGridWidths.y * 1.08);

    lvl_0 = max(lvl_0 - lvl_1, 0.0);
    lvl_1 = max(lvl_1 - lvl_2 * lod_blend, 0.0);

    float minor_alpha = lvl_0 * u_editorGridStyle.x * (1.0 - lod_blend);
    float mid_alpha = lvl_1 * mix(u_editorGridStyle.y, u_editorGridStyle.x, lod_blend);
    float major_alpha = lvl_2 * u_editorGridStyle.y * lod_blend;

    float x_axis = axis_mask(world_xz.y, u_editorGridWidths.z);
    float z_axis = axis_mask(world_xz.x, u_editorGridWidths.z);
    float axis = max(x_axis, z_axis);

    minor_alpha *= 1.0 - axis;
    mid_alpha *= 1.0 - axis;
    major_alpha *= 1.0 - axis;

    float grazing = abs(ray.y);
    float grazing_fade = smoothstep(0.0015, 0.012, grazing);
    float forward = step(1e-12, plane_offset * ray.y);
    float gate = forward * far_fade * grazing_fade;

    minor_alpha *= gate;
    mid_alpha *= gate;
    major_alpha *= gate;

    float x_axis_alpha = x_axis * u_editorGridStyle.z * gate;
    float z_axis_alpha = z_axis * u_editorGridStyle.z * gate;

    float total = minor_alpha + mid_alpha + major_alpha + x_axis_alpha + z_axis_alpha;
    float alpha = max(max(max(minor_alpha, mid_alpha), major_alpha), max(x_axis_alpha, z_axis_alpha));

    if (alpha <= 0.001) {
        discard;
    }

    float znear = u_editorGridOrigin.z;
    float view_z = dot(u_editorGridBasisForward.xyz, hit - u_editorGridCameraPos.xyz);
    if (is_ortho) {
        float zfar = u_editorGridOrigin.w;
        gl_FragDepth = clamp((zfar - view_z) / max(zfar - znear, 1e-6), 0.0, 1.0);
    } else {
        gl_FragDepth = clamp(znear / max(view_z, znear), 0.0, 1.0);
    }

    vec3 minor_color = vec3(0.28, 0.28, 0.28);
    vec3 major_color = vec3(0.42, 0.42, 0.42);
    vec3 x_axis_color = vec3(0.78, 0.18, 0.18);
    vec3 z_axis_color = vec3(0.18, 0.62, 0.24);

    vec3 color = (minor_color * minor_alpha
        + major_color * (mid_alpha + major_alpha)
        + x_axis_color * x_axis_alpha
        + z_axis_color * z_axis_alpha) / max(total, 0.0001);

    gl_FragColor = vec4(color, alpha);
}
