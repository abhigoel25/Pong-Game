import machine  # Library to control ESP32 hardware (UART, Pins, etc.)
import time     # Library for delays and timing
import random   # Library to make the ball start in random directions

# --- 1. DISPLAY SETUP ---
import lcd      # The driver for the Elecrow 7-inch screen
lcd.init()      # Wake up the screen pixels
lcd.rotation(0) # Set screen orientation (0-3)
BLACK = 0x0000  # Color code for Black (Hexadecimal)
WHITE = 0xFFFF  # Color code for White
GREEN = 0x07E0  # Color code for Green

# --- 2. HARDWARE & CONSTANTS ---
# UART 2: Listens to the Arduino on Pin 16 (RX). Baudrate must match Arduino (115200).
uart = machine.UART(2, baudrate=115200, rx=16, tx=17)

WIDTH, HEIGHT = 800, 480    # The pixel resolution of your 7-inch screen
PADDLE_W, PADDLE_H = 15, 90 # Size of the paddles in pixels
BALL_SIZE = 12              # Size of the ball square in pixels

# Game State Variables
score1, score2 = 0, 0       # Starting scores
ball_x, ball_y = WIDTH//2, HEIGHT//2  # Ball starts in the middle
old_ball_x, old_ball_y = ball_x, ball_y # Tracks previous ball spot for "clean erasing"
p1_y, p2_y = 200, 200       # Starting heights for paddles
old_p1_y, old_p2_y = p1_y, p2_y # Tracks previous paddle spots
ball_dx, ball_dy = 6, 6     # Initial speed of the ball (Pixels per frame)
hit_count = 0               # Counts hits to trigger the "speed up"
game_active = False         # False = Game is "Off" or "Paused"

# SETTINGS: 0=Easy, 1=Medium, 2=Impossible
difficulty = 1              # Set your bot challenge level here
two_player_mode = False     # Set to True to use both sliders for 2 humans

# --- 3. FUNCTIONS ---

def reset_ball():
    """Resets the ball to the center and clears the screen."""
    global ball_x, ball_y, ball_dx, ball_dy, hit_count
    lcd.fill(BLACK)         # Wipe the screen clean
    ball_x, ball_y = WIDTH // 2, HEIGHT // 2 # Center coordinates
    ball_dx = 6 if random.random() > 0.5 else -6 # Random Left/Right start
    ball_dy = random.choice([-5, 5])            # Random Up/Down start
    hit_count = 0           # Reset hit counter for the new round

def handle_hit():
    """Logic for when the ball hits a paddle."""
    global ball_dx, ball_dy, hit_count
    ball_dx *= -1           # Reverse horizontal direction
    hit_count += 1          # Increment the hit counter
    # PROJECT RULE: Speed up every 2 hits
    if hit_count % 2 == 0:
        ball_dx *= 1.2      # Increase horizontal speed by 20%
        ball_dy *= 1.2      # Increase vertical speed by 20%

def update_logic(p1_raw, p2_raw):
    """Calculates movement, collisions, and scoring."""
    global ball_x, ball_y, ball_dx, ball_dy, p1_y, p2_y, score1, score2

    # Map Arduino analog data (0-1023) to Screen height (0-480)
    p1_y = int((p1_raw / 1023) * (HEIGHT - PADDLE_H))
    
    if two_player_mode:
        # If 2-Player, map the second slider to Player 2
        p2_y = int((p2_raw / 1023) * (HEIGHT - PADDLE_H))
    else:
        # --- BOT DIFFICULTY LOGIC ---
        if difficulty == 0:     # EASY: Slow, constant bot
            ai_speed = 4
        elif difficulty == 1:   # MEDIUM: Fast, but only reacts when ball is close
            ai_speed = 7 if (ball_dx > 0 and ball_x > WIDTH // 2) else 0
        else:                   # IMPOSSIBLE: Always matches ball vertical speed
            ai_speed = abs(ball_dy) + 2
            
        # Move Bot Paddle center toward the ball Y position
        if (p2_y + PADDLE_H//2) < ball_y:
            p2_y += ai_speed
        else:
            p2_y -= ai_speed

    # Move the ball based on its current speed
    ball_x += ball_dx
    ball_y += ball_dy

    # Bounce off Top and Bottom walls
    if ball_y <= 0 or ball_y >= HEIGHT - BALL_SIZE:
        ball_dy *= -1
    
    # Check Collision with Player 1 (Left)
    if ball_x <= PADDLE_W and p1_y < ball_y < p1_y + PADDLE_H:
        handle_hit()
        
    # Check Collision with Player 2/Bot (Right)
    if ball_x >= WIDTH - PADDLE_W - BALL_SIZE and p2_y < ball_y < p2_y + PADDLE_H:
        handle_hit()

    # Score Point for Player 2 (Ball went off left)
    if ball_x < 0:
        score2 += 1
        reset_ball()
    # Score Point for Player 1 (Ball went off right)
    elif ball_x > WIDTH:
        score1 += 1
        reset_ball()

def draw_frame():
    """Redraws the screen using an 'erase-then-draw' method to stop flickering."""
    global old_ball_x, old_ball_y, old_p1_y, old_p2_y
    # Erase the OLD positions by drawing black boxes over them
    lcd.fill_rect(old_ball_x, old_ball_y, BALL_SIZE, BALL_SIZE, BLACK)
    lcd.fill_rect(0, old_p1_y, PADDLE_W, PADDLE_H, BLACK)
    lcd.fill_rect(WIDTH - PADDLE_W, old_p2_y, PADDLE_W, PADDLE_H, BLACK)
    
    # Draw the NEW positions
    lcd.fill_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE, WHITE)  # Ball
    lcd.fill_rect(0, p1_y, PADDLE_W, PADDLE_H, GREEN)           # Player 1 (Green)
    lcd.fill_rect(WIDTH - PADDLE_W, p2_y, PADDLE_W, PADDLE_H, WHITE) # Player 2/Bot
    
    # Store current spots so we can erase them in the next frame
    old_ball_x, old_ball_y = ball_x, ball_y
    old_p1_y, old_p2_y = p1_y, p2_y

# --- 4. MAIN PROGRAM LOOP ---
reset_ball() # Initialize ball for the first time
while True:
    if uart.any(): # Check if there is new data from the Arduino
        try:
            line = uart.readline().decode().strip() # Read the "P1,P2,Power" string
            data = line.split(',')                  # Split string into a list
            if len(data) == 3:                      # Ensure we got all 3 values
                p1_val = int(data[0])               # P1 Slider
                p2_val = int(data[1])               # P2 Slider
                game_active = (data[2] == "1")      # Power Button status (1=ON)

                if game_active:
                    # If game is ON and no one has won yet (First to 3)
                    if score1 < 3 and score2 < 3:
                        update_logic(p1_val, p2_val) # Calculate math
                        draw_frame()                 # Update screen
                    else:
                        lcd.text("WINNER!", WIDTH//2, HEIGHT//2, WHITE) # End screen
                else:
                    # If power is "OFF", black out screen and show status
                    lcd.fill(BLACK)
                    lcd.text("SYSTEM OFF", WIDTH//2 - 40, HEIGHT//2, WHITE)
        except:
            pass # Skip frame if serial data is corrupted
    time.sleep_ms(5) # Delay for 5ms to keep game speed consistent