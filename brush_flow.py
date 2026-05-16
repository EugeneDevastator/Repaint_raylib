# brush_flow.py
import pyray as rl
import math

SCREEN_W = 1920
SCREEN_H = 1080
FONT_SIZE = 32
TEX_W, TEX_H = 512, 512

_FIT = 0.5 / (math.sqrt(2) / 2)

QUAD_FRAG = f"""
#version 330
in vec2 fragTexCoord;
out vec4 finalColor;

uniform float u_angle;
uniform float u_squish;
uniform float u_size;

void main() {{
    vec2 uv = fragTexCoord;
    vec2 p = uv - 0.5;

    float c = cos(u_angle), s = sin(u_angle);
    vec2 r = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
    r.x /= u_squish;
    r /= (u_size * {_FIT:.6f});

    if (abs(r.x) > 0.5 || abs(r.y) > 0.5) {{
        finalColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }}
    vec2 quadUV = r + 0.5;
    finalColor = vec4(quadUV.x, quadUV.y, 0.0, 1.0);
}}
"""

BG_FRAG = """
#version 330
in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
    if (fragTexCoord.x < 0.5)
        finalColor = vec4(0.0, 0.2, 1.0, 1.0);
    else
        finalColor = vec4(1.0, 1.0, 0.0, 1.0);
}
"""

CIRCLE_FRAG = """
#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;

void main() {
    vec2 uv = fragTexCoord;
    vec2 sampleUV = vec2(uv.x, 1.0 - uv.y);

    vec4 bg      = texture(texture1, uv);
    vec4 sampled = texture(texture0, sampleUV);

    if (sampled.a < 0.01) {
        finalColor = bg;
        return;
    }

    vec2 p     = sampled.rg - 0.5;
    float dist = length(p);
    float circle = smoothstep(0.5, 0.1, dist);

    vec4 mytex = texture(texture2, sampled.rg);
    vec4 brush = vec4(mytex.rgb, circle);

    bool isBlue = (bg.b > bg.r && bg.b > bg.g);
    finalColor.rgb = brush.rgb;
    if (isBlue) {
        finalColor = mix(bg, brush, circle);
        finalColor.a = 1;
    } else {
        finalColor = bg;
    }
}
"""

DEFAULT_VERT = """
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 fragTexCoord;
uniform mat4 mvp;
void main() {
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
"""

def main():
    rl.init_window(SCREEN_W, SCREEN_H, "Shader Pipeline")
    rl.set_target_fps(60)

    quad_shader   = rl.load_shader_from_memory(DEFAULT_VERT, QUAD_FRAG)
    bg_shader     = rl.load_shader_from_memory(DEFAULT_VERT, BG_FRAG)
    circle_shader = rl.load_shader_from_memory(DEFAULT_VERT, CIRCLE_FRAG)

    angle_loc  = rl.get_shader_location(quad_shader, "u_angle")
    squish_loc = rl.get_shader_location(quad_shader, "u_squish")
    size_loc   = rl.get_shader_location(quad_shader, "u_size")

    tex1_loc = rl.get_shader_location(circle_shader, "texture1")
    tex2_loc = rl.get_shader_location(circle_shader, "texture2")

    intermediate_rt = rl.load_render_texture(TEX_W, TEX_H)
    bg_rt           = rl.load_render_texture(TEX_W, TEX_H)
    final_rt        = rl.load_render_texture(TEX_W, TEX_H)

    white_img = rl.gen_image_color(1, 1, rl.WHITE)
    white_tex = rl.load_texture_from_image(white_img)
    rl.unload_image(white_img)

    my_tex = rl.load_texture("mytex.png")

    full_src = rl.Rectangle(0, 0, TEX_W, TEX_H)
    full_dst = rl.Rectangle(0, 0, TEX_W, TEX_H)
    origin   = rl.Vector2(0, 0)

    # bake bg once
    rl.begin_texture_mode(bg_rt)
    rl.clear_background(rl.Color(0, 0, 0, 255))
    rl.begin_shader_mode(bg_shader)
    rl.draw_texture_pro(white_tex, rl.Rectangle(0,0,1,1), full_dst, origin, 0.0, rl.WHITE)
    rl.end_shader_mode()
    rl.end_texture_mode()

    src_rect  = rl.Rectangle(0, 0, TEX_W, -TEX_H)

    PANEL_X   = 100
    PANEL_GAP = 60
    PANEL_Y   = 190
    LABEL_Y   = 150

    dst_bg    = rl.Rectangle(PANEL_X + TEX_W + PANEL_GAP,       PANEL_Y, TEX_W, TEX_H)
    dst_final = rl.Rectangle(PANEL_X + (TEX_W + PANEL_GAP) * 2, PANEL_Y, TEX_W, TEX_H)

    t = 0.0

    while not rl.window_should_close():
        dt = rl.get_frame_time()
        t += dt

        angle  = t * 1.5
        squish = 0.75 + 0.5 * math.sin(t * 0.3)
        size   = 0.65 + 0.25 * math.sin(t * 0.18)   # range 0.4–0.9, period ~35 s

        rl.set_shader_value(quad_shader, angle_loc,  rl.ffi.new("float[1]", [angle]),  rl.SHADER_UNIFORM_FLOAT)
        rl.set_shader_value(quad_shader, squish_loc, rl.ffi.new("float[1]", [squish]), rl.SHADER_UNIFORM_FLOAT)
        rl.set_shader_value(quad_shader, size_loc,   rl.ffi.new("float[1]", [size]),   rl.SHADER_UNIFORM_FLOAT)

        rl.begin_texture_mode(intermediate_rt)
        rl.clear_background(rl.Color(0, 0, 0, 255))
        rl.begin_shader_mode(quad_shader)
        rl.draw_texture_pro(white_tex, rl.Rectangle(0,0,1,1), full_dst, origin, 0.0, rl.WHITE)
        rl.end_shader_mode()
        rl.end_texture_mode()

        rl.begin_texture_mode(final_rt)
        rl.clear_background(rl.Color(0, 0, 0, 0))
        rl.begin_shader_mode(circle_shader)
        rl.set_shader_value_texture(circle_shader, tex1_loc, bg_rt.texture)
        rl.set_shader_value_texture(circle_shader, tex2_loc, my_tex)
        rl.draw_texture_pro(intermediate_rt.texture, full_src, full_dst, origin, 0.0, rl.WHITE)
        rl.end_shader_mode()
        rl.end_texture_mode()

        # first panel: intermediate drawn smaller, centered on green bg
        px = PANEL_X
        py = PANEL_Y
        drawn_size = int(TEX_W * size)
        offset_x = (TEX_W - drawn_size) // 2
        offset_y = (TEX_H - drawn_size) // 2
        dst_intermediate = rl.Rectangle(px + offset_x, py + offset_y, drawn_size, drawn_size)

        rl.begin_drawing()
        rl.clear_background(rl.RAYWHITE)

        # green bg for panel 1
        rl.draw_rectangle(px, py, TEX_W, TEX_H, rl.Color(30, 160, 30, 255))
        rl.draw_texture_pro(intermediate_rt.texture, src_rect, dst_intermediate, origin, 0.0, rl.WHITE)

        rl.draw_texture_pro(bg_rt.texture,    src_rect, dst_bg,    origin, 0.0, rl.WHITE)
        rl.draw_texture_pro(final_rt.texture, src_rect, dst_final, origin, 0.0, rl.WHITE)

        rl.draw_text("UV Quad (size animated)", PANEL_X,                          LABEL_Y, FONT_SIZE, rl.DARKGRAY)
        rl.draw_text("Background",              PANEL_X + TEX_W + PANEL_GAP,      LABEL_Y, FONT_SIZE, rl.DARKGRAY)
        rl.draw_text("Blend (blue only)",       PANEL_X + (TEX_W+PANEL_GAP) * 2,  LABEL_Y, FONT_SIZE, rl.DARKGRAY)

        rl.end_drawing()

    rl.unload_texture(white_tex)
    rl.unload_texture(my_tex)
    rl.unload_render_texture(intermediate_rt)
    rl.unload_render_texture(bg_rt)
    rl.unload_render_texture(final_rt)
    rl.unload_shader(quad_shader)
    rl.unload_shader(bg_shader)
    rl.unload_shader(circle_shader)
    rl.close_window()

if __name__ == "__main__":
    main()
