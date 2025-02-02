#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define CLK 34
#define DT 39
#define BTN1 27
#define BTN2 14
#define BTN3 12
#define BTN4 13
#define MPIN 26
// #define RTOS
double motorMinSpeed = 1800; // change motor default voltage at 0
// double motorMinSpeed = 0; // change motor default voltage at 0
double motorMaxSpeed = 2338;
double counterMinSpeed = -250;
double counterMaxSpeed = 250;
int doubleTapTime = 160; // double Tap timing
volatile double counter = 0;
volatile double rotCounter = 0;
volatile uint64_t pwm = 0;
volatile bool btn1State = false, btn2State = false, btn3State = false, btn4State = false;
volatile bool isPlaying = false;
volatile int lastStateCLK = LOW;
volatile double lastpwm = 0;

const int PWM_CHANNEL = 0;     // ESP32 has 16 channels which can generate 16 independent waveforms
const int PWM_FREQ = 19530;    // Recall that Arduino Uno is ~490 Hz. Official ESP32 example uses 5,000Hz
const int PWM_RESOLUTION = 12; // We'll use same resolution as Uno (8 bits, 0-255) but ESP32 can go up to 16 bits

// The max duty cycle value based on PWM resolution (will be 255 if resolution is 8 bits)
const uint64_t MAX_DUTY_CYCLE = (int)(pow(2, PWM_RESOLUTION) - 1);

void IRAM_ATTR updateEncoder()
{
    int currentStateCLK = digitalRead(CLK);
    double boost = 0.1; // Shift plus encoder step increase/decrease
    double step = 1;    // encoder increase/decrease step
    if (currentStateCLK != lastStateCLK)
    {

        if (digitalRead(DT) != currentStateCLK)
        {
            btn4State ? rotCounter += boost : rotCounter += step;
        }
        else
        {
            btn4State ? rotCounter -= boost : rotCounter -= step;
        }
        rotCounter = constrain(rotCounter, counterMinSpeed, counterMaxSpeed);
    }
    lastStateCLK = currentStateCLK;
}
bool _GameState = false;
//---------------------------

#define SCREEN_WIDTH 128

#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(128, 64, &Wire, -1);
TaskHandle_t ButtonTaask;
bool bypass = false; // bypass the playing and pause state

void TaskButtons(void *pvParameters)
{
    (void)pvParameters;
    double upLimit = 150, downLimit = -150; // shift plus increase tempo / shift plus decrease tempo
    double step = 1;                        // increase / decrease step

    while (1)
    {
        if (btn4State && btn1State)
        {
            vTaskSuspend(ButtonTaask);
            int lowCounter = -800; // minimum reading of shift plus play VINYL STOP effect
            bypass = true;
            if (isPlaying)
            {
                counter = rotCounter;
                for (int _pwm = counter; _pwm >= lowCounter; _pwm -= 1)
                {
                    counter = _pwm;
                    vTaskDelay(6 / portTICK_PERIOD_MS); // shift plus play vinyl stop effect - how fast it takes to stop, increase number to make it slower
                }
                counter = lowCounter;
            }
            else
            {
                counter = lowCounter;
                for (int _pwm = counter; _pwm <= rotCounter; _pwm += 1)
                {
                    counter = _pwm;
                    vTaskDelay(10 / portTICK_PERIOD_MS); // shift plus play vinyl start effect - how fast it takes to start, increase number to make it slower
                }
                counter = rotCounter;
            }
            bypass = false;
            vTaskResume(ButtonTaask);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        else if (isPlaying && btn4State && btn2State && btn3State)
        {
            counter = counterMaxSpeed;
            rotCounter = counterMaxSpeed;
        }
        else if (btn4State && btn3State) // play/pause , if button 4 and 2 are pressed
        {
            counter = upLimit;
            bypass = true;
        }
        else if (btn4State && btn2State) // play pause  , if button 4 and 3 are pressed
        {
            counter = downLimit;
            bypass = true;
        }
        else if (isPlaying && btn3State) // if playing and buttton 2 is pressed , INCREASE TEMPO button
        {
            counter < (counterMaxSpeed - step) ? counter += step : counter = counterMaxSpeed;
            vTaskDelay(7 / portTICK_PERIOD_MS); // tempo bend increase speed
        }
        else if (isPlaying && btn2State) // if pllaying and btoon 3 is pressed] , DECREASE TEMPO button
        {
            counter > (counterMinSpeed + step) ? counter -= step : counter = counterMinSpeed;
            vTaskDelay(7 / portTICK_PERIOD_MS); // tempo bend decrease speed
        }
        else
        {
            counter = rotCounter; // if nothing is pressed then go to the last speed set by rotary
            bypass = false;
        }
    }
}
void TaskMotor(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        // copnvert counter to motor signal
        pwm = map(counter, counterMinSpeed, counterMaxSpeed, motorMinSpeed, motorMaxSpeed);

        if (isPlaying || bypass)
        {
        }
        else
        {
            pwm = 0;
        }
        if (lastpwm != pwm)
        {
            lastpwm = pwm;

            // analogWrite(MPIN, pwm);
            ledcWrite(PWM_CHANNEL, pwm);
        }
    }
}
void TaskPrintScreen(void *pvParameters)
{
    (void)pvParameters;
    Serial.begin(9600);
    while (1)
    {
        Serial.print(" | Counter: ");
        Serial.print(counter);
        Serial.print(" | PWM: ");
        Serial.print(pwm);
        Serial.print(" isPlaying- ");
        Serial.print(isPlaying ? "playing" : "pause");
        Serial.print(" btn1State- ");
        Serial.print(btn1State);
        Serial.print(" btn2State- ");
        Serial.print(btn2State);
        Serial.print(" btn3State- ");
        Serial.print(btn3State);
        Serial.print(" btn4State- ");
        Serial.println(btn4State);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
void TaskUpdateScreen(void *pvParameters)
{
    (void)pvParameters;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setRotation(1);
    display.setTextSize(2);
    while (1)
    {
        display.clearDisplay();
        display.setTextSize(2);
        display.drawRect(0, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SSD1306_WHITE);
        int centerX = SCREEN_WIDTH / 2;
        display.drawLine(0, centerX, SCREEN_HEIGHT, centerX, SSD1306_WHITE);
        int counterInt = int(counter);
        int decimalCounter = int((counter - int(counter)) * 10);

        if (counterInt > 0 || decimalCounter > 0)
        {
            display.setCursor(42, 5);
            display.print("+");
        }
        else if (counterInt < 0 || decimalCounter < 0)
        {
            display.setCursor(12, 5);
            display.print("-");
        }

        int absCounter = abs(counterInt);
        if (absCounter > 9)
        {
            display.setCursor(15, 25);
            if (absCounter > 99)
                display.setCursor(5, 25);
        }
        else
            display.setCursor(25, 25);

        display.setTextSize(3);
        display.print(absCounter);
        display.setTextSize(1);
        display.setCursor(30, 50);
        if (decimalCounter != 0)
            display.print(abs(decimalCounter));
        display.setTextSize(2);
        display.setCursor(10, 75);
        if (isPlaying)
        {
            display.print("PLAY");
            display.fillTriangle(26, 100, 26, 120, 42, 110, SSD1306_WHITE);
        }
        else
        {
            display.print("STOP");
            display.fillRect(26, 100, 16, 16, SSD1306_WHITE);
        }
        display.display();
        // vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
void TaskState(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        btn1State = !digitalRead(BTN1);
        if (btn1State)
            delay(100);
        while (!digitalRead(BTN1))
        {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        btn1State ? isPlaying = !isPlaying : NULL;
        btn2State = !digitalRead(BTN2);
        btn3State = !digitalRead(BTN3);
        btn4State = !digitalRead(BTN4);
        if (btn4State)
        {
            uint64_t t = millis();
            delay(100);
            bool tap = false;
            while (millis() - t < doubleTapTime)
            {
                btn4State = !digitalRead(BTN4);
                if (!btn4State)
                    tap = true;
                if (tap && btn4State)
                {
                    counter = 0;
                    rotCounter = 0;
                    break;
                }
            }
        }
    }
}
//-------

#define WIDTH 64

#define HEIGHT 128

const char pieces_S_l[2][2][4] = {{

                                      {0, 0, 1, 1}, {0, 1, 1, 2}

                                  },

                                  {

                                      {0, 1, 1, 2}, {1, 1, 0, 0}

                                  }};

const char pieces_S_r[2][2][4]{{

                                   {1, 1, 0, 0}, {0, 1, 1, 2}

                               },

                               {

                                   {0, 1, 1, 2}, {0, 0, 1, 1}

                               }};

const char pieces_L_l[4][2][4] = {{

                                      {0, 0, 0, 1}, {0, 1, 2, 2}

                                  },

                                  {

                                      {0, 1, 2, 2}, {1, 1, 1, 0}

                                  },

                                  {

                                      {0, 1, 1, 1}, {0, 0, 1, 2}

                                  },

                                  {

                                      {0, 0, 1, 2}, {1, 0, 0, 0}

                                  }};

const char pieces_Sq[1][2][4] = {{

    {0, 1, 0, 1}, {0, 0, 1, 1}

}};

const char pieces_T[4][2][4] = {{

                                    {0, 0, 1, 0}, {0, 1, 1, 2}

                                },

                                {

                                    {0, 1, 1, 2}, {1, 0, 1, 1}

                                },

                                {

                                    {1, 0, 1, 1}, {0, 1, 1, 2}

                                },

                                {

                                    {0, 1, 1, 2}, {0, 0, 1, 0}

                                }};

const char pieces_l[2][2][4] = {{

                                    {0, 1, 2, 3}, {0, 0, 0, 0}

                                },

                                {

                                    {0, 0, 0, 0}, {0, 1, 2, 3}

                                }};

const short MARGIN_TOP = 19;

const short MARGIN_LEFT = 3;

const short SIZE = 5;

const short TYPES = 6;

#define SPEAKER_PIN 8

const int MELODY_LENGTH = 10;

const int MELODY_NOTES[MELODY_LENGTH] = {262, 294, 330, 262};

const int MELODY_DURATIONS[MELODY_LENGTH] = {500, 500, 500, 500};

int click[] = {1047};

int click_duration[] = {100};

int erase[] = {2093};

int erase_duration[] = {100};

word currentType, nextType, rotation;

short pieceX, pieceY;

short piece[2][4];

int interval = 20, score;

long timer, delayer;

boolean grid[10][18];

boolean b1, b2, b3;

#define left 14

#define right 12

#define change 27

#define speed 13

void checkLines()
{

    boolean full;

    for (short y = 17; y >= 0; y--)
    {

        full = true;

        for (short x = 0; x < 10; x++)
        {

            full = full && grid[x][y];
        }

        if (full)
        {

            breakLine(y);

            y++;
        }
    }
}

void breakLine(short line)
{
    // tone(SPEAKER_PIN, erase[0], 1000 / erase_duration[0]);
#ifdef RTOS
    vTaskDelay(100 / portTICK_PERIOD_MS);
#else
    delay(100);
#endif
    // noTone(SPEAKER_PIN);
    for (short y = line; y >= 0; y--)
    {
        for (short x = 0; x < 10; x++)
        {

            grid[x][y] = grid[x][y - 1];
        }
    }

    for (short x = 0; x < 10; x++)
    {

        grid[x][0] = 0;
    }

    display.invertDisplay(true);

#ifdef RTOS
    vTaskDelay(50 / portTICK_PERIOD_MS);
#else
    delay(50);
#endif

    display.invertDisplay(false);

    score += 10;
}

void refresh()
{

    display.clearDisplay();

    drawLayout();

    drawGrid();

    drawPiece(currentType, 0, pieceX, pieceY);

    display.display();
}

void drawGrid()
{

    for (short x = 0; x < 10; x++)

        for (short y = 0; y < 18; y++)

            if (grid[x][y])

                display.fillRect(MARGIN_LEFT + (SIZE + 1) * x, MARGIN_TOP + (SIZE + 1) * y, SIZE, SIZE, WHITE);
}

boolean nextHorizontalCollision(short piece[2][4], int amount)
{

    for (short i = 0; i < 4; i++)
    {

        short newX = pieceX + piece[0][i] + amount;

        if (newX > 9 || newX < 0 || grid[newX][pieceY + piece[1][i]])

            return true;
    }

    return false;
}

boolean nextCollision()
{

    for (short i = 0; i < 4; i++)
    {

        short y = pieceY + piece[1][i] + 1;

        short x = pieceX + piece[0][i];

        if (y > 17 || grid[x][y])

            return true;
    }

    return false;
}

void generate()
{

    currentType = nextType;

    nextType = random(TYPES);

    if (currentType != 5)

        pieceX = random(9);

    else

        pieceX = random(7);

    pieceY = 0;

    rotation = 0;

    copyPiece(piece, currentType, rotation);
}

void drawPiece(short type, short rotation, short x, short y)
{

    for (short i = 0; i < 4; i++)

        display.fillRect(MARGIN_LEFT + (SIZE + 1) * (x + piece[0][i]), MARGIN_TOP + (SIZE + 1) * (y + piece[1][i]), SIZE, SIZE, WHITE);
}

void drawNextPiece()
{

    short nPiece[2][4];

    copyPiece(nPiece, nextType, 0);

    for (short i = 0; i < 4; i++)

        display.fillRect(50 + 3 * nPiece[0][i], 4 + 3 * nPiece[1][i], 2, 2, WHITE);
}

void copyPiece(short piece[2][4], short type, short rotation)
{

    switch (type)
    {

    case 0: // L_l

        for (short i = 0; i < 4; i++)
        {

            piece[0][i] = pieces_L_l[rotation][0][i];

            piece[1][i] = pieces_L_l[rotation][1][i];
        }

        break;

    case 1: // S_l

        for (short i = 0; i < 4; i++)
        {

            piece[0][i] = pieces_S_l[rotation][0][i];

            piece[1][i] = pieces_S_l[rotation][1][i];
        }

        break;

    case 2: // S_r

        for (short i = 0; i < 4; i++)
        {

            piece[0][i] = pieces_S_r[rotation][0][i];

            piece[1][i] = pieces_S_r[rotation][1][i];
        }

        break;

    case 3: // Sq

        for (short i = 0; i < 4; i++)
        {

            piece[0][i] = pieces_Sq[0][0][i];

            piece[1][i] = pieces_Sq[0][1][i];
        }

        break;

    case 4: // T

        for (short i = 0; i < 4; i++)
        {

            piece[0][i] = pieces_T[rotation][0][i];

            piece[1][i] = pieces_T[rotation][1][i];
        }

        break;

    case 5: // l

        for (short i = 0; i < 4; i++)
        {

            piece[0][i] = pieces_l[rotation][0][i];

            piece[1][i] = pieces_l[rotation][1][i];
        }

        break;
    }
}

short getMaxRotation(short type)
{

    if (type == 1 || type == 2 || type == 5)

        return 2;

    else if (type == 0 || type == 4)

        return 4;

    else if (type == 3)

        return 1;

    else

        return 0;
}

boolean canRotate(short rotation)
{

    short piece[2][4];

    copyPiece(piece, currentType, rotation);

    return !nextHorizontalCollision(piece, 0);
}

void drawLayout()
{

    display.drawLine(0, 15, WIDTH, 15, WHITE);

    display.drawRect(0, 0, WIDTH, HEIGHT, WHITE);

    drawNextPiece();

    char text[6];

    itoa(score, text, 10);

    drawText(text, getNumberLength(score), 7, 4);
}

short getNumberLength(int n)
{

    short counter = 1;

    while (n >= 10)
    {

        n /= 10;

        counter++;
    }

    return counter;
}

void drawText(char text[], short length, int x, int y)
{

    display.setTextSize(1); // Normal 1:1 pixel scale

    display.setTextColor(WHITE); // Draw white text

    display.setCursor(x, y); // Start at top-left corner

    display.cp437(true); // Use full 256 char 'Code Page 437' font

    for (short i = 0; i < length; i++)

        display.write(text[i]);
}

void Game()
{
    display.setRotation(1);

    display.clearDisplay();

    drawLayout();

    display.display();

    // randomSeed(analogRead(0));

    nextType = random(TYPES);

    generate();

    timer = millis();
    while (1)
    {
        if (millis() - timer > interval)
        {

            checkLines();

            refresh();

            if (nextCollision())
            {

                for (short i = 0; i < 4; i++)

                    grid[pieceX + piece[0][i]][pieceY + piece[1][i]] = 1;

                generate();
            }
            else

                pieceY++;

            timer = millis();
        }

        if (!digitalRead(left))
        {

            // tone(SPEAKER_PIN, click[0], 1000 / click_duration[0]);

#ifdef RTOS
            vTaskDelay(100 / portTICK_PERIOD_MS);
#else
            delay(100);
#endif

            // noTone(SPEAKER_PIN);

            if (b1)
            {

                if (!nextHorizontalCollision(piece, -1))
                {

                    pieceX--;

                    refresh();
                }

                b1 = false;
            }
        }
        else
        {

            b1 = true;
        }

        if (!digitalRead(right))
        {

            // tone(SPEAKER_PIN, click[0], 1000 / click_duration[0]);

#ifdef RTOS
            vTaskDelay(100 / portTICK_PERIOD_MS);
#else
            delay(100);
#endif

            // noTone(SPEAKER_PIN);

            if (b2)
            {

                if (!nextHorizontalCollision(piece, 1))
                {

                    pieceX++;

                    refresh();
                }

                b2 = false;
            }
        }
        else
        {

            b2 = true;
        }

        if (!digitalRead(speed))
        {

            interval = 20;
        }
        else
        {

            interval = 400;
        }

        if (!digitalRead(change))
        {

            // tone(SPEAKER_PIN, click[0], 1000 / click_duration[0]);

#ifdef RTOS
            vTaskDelay(100 / portTICK_PERIOD_MS);
#else
            delay(100);
#endif

            // noTone(SPEAKER_PIN);

            if (b3)
            {

                if (rotation == getMaxRotation(currentType) - 1 && canRotate(0))
                {

                    rotation = 0;
                }
                else if (canRotate(rotation + 1))
                {

                    rotation++;
                }

                copyPiece(piece, currentType, rotation);

                refresh();

                b3 = false;

                delayer = millis();
            }
        }
        else if (millis() - delayer > 50)
        {

            b3 = true;
        }
#ifdef RTOS
        vTaskDelay(10 / portTICK_PERIOD_MS);
#else
        delay(10);
#endif
    }
}
void setup()
{
    Serial.begin(9600);

    pinMode(BTN1, INPUT_PULLUP);
    pinMode(BTN2, INPUT_PULLUP);
    pinMode(BTN3, INPUT_PULLUP);
    pinMode(BTN4, INPUT_PULLUP);

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        while (1)
        {
#ifdef RTOS
            vTaskDelay(100 / portTICK_PERIOD_MS);
#else
            delay(100);
#endif
        }
    }
    uint64_t t = millis();
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setRotation(1);
    display.setTextSize(1);
    display.setCursor(7, 50);
    display.print("DESIGNED");
    display.setCursor(25, 60);
    display.print("BY");
    display.setCursor(10, 70);
    display.print("NAVENDU");
    display.display();
    while (millis() - t < 2000) // 3 seconds , 1 second = 1000 milliseconds
    {
        _GameState = !digitalRead(BTN1);
        if (_GameState)
            break;
    }
    if (_GameState)
    {
#ifdef RTOS
        xTaskCreate(game, "TaskGAme", 16384, NULL, 1, NULL);
#else
        delay(100);
#endif
    }
    else
    {
        pwm = map(counter, counterMinSpeed, counterMaxSpeed, motorMinSpeed, motorMaxSpeed);
        pinMode(CLK, INPUT);
        pinMode(DT, INPUT);
        // pinMode(MPIN, OUTPUT);
        ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(MPIN, PWM_CHANNEL);
        attachInterrupt(digitalPinToInterrupt(CLK), updateEncoder, CHANGE);
        attachInterrupt(digitalPinToInterrupt(DT), updateEncoder, CHANGE);
        xTaskCreatePinnedToCore(TaskState, "TaskSTATE", 4096, NULL, 1, &ButtonTaask, ARDUINO_RUNNING_CORE);
        xTaskCreatePinnedToCore(TaskButtons, "TaskBTN", 1024, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
        xTaskCreatePinnedToCore(TaskUpdateScreen, "TaskScreen", 2048, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
        // xTaskCreatePinnedToCore(TaskPrintScreen, "TaskPrint", 2048, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
        xTaskCreatePinnedToCore(TaskMotor, "TaskPrint", 2048, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
    }
}
void loop()
{
#ifdef RTOS

#else
    if (_GameState)
        Game();
#endif
}
