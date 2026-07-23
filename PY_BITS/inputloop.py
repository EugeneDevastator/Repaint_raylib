import pygame
import math

pygame.init()

WIDTH, HEIGHT = 900, 650
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("Paint with Throttle")

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
GRAY = (200, 200, 200)
DARK_GRAY = (120, 120, 120)
BLUE = (70, 130, 220)
RED = (220, 60, 60)

font = pygame.font.SysFont("Arial", 22)

SLIDER_X = 30
SLIDER_Y = 30
SLIDER_W = 200
SLIDER_H = 20
throttle = 25
dragging_slider = False

last_point = None
drawing = False

canvas = pygame.Surface((WIDTH, HEIGHT))
canvas.fill(WHITE)


def throttle_to_x(t):
    return SLIDER_X + int((t / 50) * SLIDER_W)

def x_to_throttle(x):
    return max(0, min(50, int((x - SLIDER_X) / SLIDER_W * 50)))

def EstimateIfNeedDrawSegment(current_pos, last_pos, step):
    dist = math.hypot(current_pos[0] - last_pos[0], current_pos[1] - last_pos[1])
    return dist >= max(1, step)


clock = pygame.time.Clock()

while True:
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            pygame.quit()
            exit()

        if event.type == pygame.MOUSEBUTTONDOWN:
            mx, my = event.pos
            if SLIDER_Y - 10 <= my <= SLIDER_Y + SLIDER_H + 10 and SLIDER_X <= mx <= SLIDER_X + SLIDER_W:
                dragging_slider = True
                throttle = x_to_throttle(mx)
            else:
                drawing = True
                last_point = (mx, my)

        if event.type == pygame.MOUSEBUTTONUP:
            dragging_slider = False
            drawing = False
            last_point = None

        if event.type == pygame.MOUSEMOTION:
            mx, my = event.pos
            if dragging_slider:
                throttle = x_to_throttle(mx)
            elif drawing and last_point:
                if EstimateIfNeedDrawSegment((mx, my), last_point, throttle):
                    pygame.draw.line(canvas, BLACK, last_point, (mx, my), 2)
                    last_point = (mx, my)

    screen.blit(canvas, (0, 0))

    pygame.draw.rect(screen, GRAY, (SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H), border_radius=10)
    fill_w = int((throttle / 50) * SLIDER_W)
    pygame.draw.rect(screen, BLUE, (SLIDER_X, SLIDER_Y, fill_w, SLIDER_H), border_radius=10)
    hx = throttle_to_x(throttle)
    pygame.draw.circle(screen, RED, (hx, SLIDER_Y + SLIDER_H // 2), 12)
    pygame.draw.circle(screen, WHITE, (hx, SLIDER_Y + SLIDER_H // 2), 7)

    label = font.render(f"Throttle (step distance): {throttle} px", True, BLACK)
    screen.blit(label, (SLIDER_X + SLIDER_W + 20, SLIDER_Y))

    hint = font.render("Draw on canvas — lines snap every throttle px", True, DARK_GRAY)
    screen.blit(hint, (SLIDER_X, SLIDER_Y + 35))

    pygame.display.flip()
    clock.tick(60)
