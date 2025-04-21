#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <atomic>
using namespace std;
#define io uint8_t
#define fio for (io i = 0; i < 4; i++)
#define CLK 34
#define DT 39
const io buttonPins[4] = {27, 14, 12, 13};

#define SCREEN_WIDTH 128

#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(128, 64, &Wire, -1);

volatile atomic<bool> buttonPushed[4];
volatile atomic<bool> play, shift;
volatile atomic<int> currentSpeed, stopSpeed, rotaryCurrentSpeed;
volatile atomic<int> speedSetterMax, speedSetterMin;
volatile atomic<int> oledShowSpeed;
volatile const int rotMin = -50, rotMax = 100;
volatile const int buttonUPMax = 75, buttonDownMin = -25;
volatile atomic<bool> oledShow;
enum buttonNames
{
    buttonToggle = 0,
    buttonFast = 1,
    buttonSlow = 2,
    buttonShift = 3
};
enum states
{
    stateToggle = 0,
    stateShiftUp = 1,
    stateShiftDown = 2
};

TaskHandle_t runTaskHandle = NULL;

struct TaskMotorParam
{
    int speed;
    int max;
    int min;
    states state;
};
const int PWM_CHANNEL = 0;     // ESP32 has 16 channels which can generate 16 independent waveforms
const int PWM_FREQ = 19530;    // Recall that Arduino Uno is ~490 Hz. Official ESP32 example uses 5,000Hz
const int PWM_RESOLUTION = 12; // We'll use same resolution as Uno (8 bits, 0-255) but ESP32 can go up to 16 bits

// The max duty cycle value based on PWM resolution (will be 255 if resolution is 8 bits)
const uint64_t MAX_DUTY_CYCLE = (int)(pow(2, PWM_RESOLUTION) - 1);
volatile int lastStateCLK = LOW;

void IRAM_ATTR updateEncoder()
{
    int currentStateCLK = digitalRead(CLK);
    if (currentStateCLK != lastStateCLK)
    {
        int delta = (digitalRead(DT) != currentStateCLK) ? 1 : -1;
        int cs = rotaryCurrentSpeed.load() + delta;
        cs = constrain(cs, rotMin, rotMax);
        rotaryCurrentSpeed.store(cs);
        currentSpeed.store(cs);
    }
    lastStateCLK = currentStateCLK;
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
        if (oledShow)
            oledShowSpeed.store(currentSpeed.load());
        display.clearDisplay();
        display.setTextSize(2);
        display.drawRect(0, 0, SCREEN_HEIGHT, SCREEN_WIDTH, SSD1306_WHITE);
        int centerX = SCREEN_WIDTH / 2;
        display.drawLine(0, centerX, SCREEN_HEIGHT, centerX, SSD1306_WHITE);
        int counterInt = int(oledShowSpeed.load());
        int decimalCounter = int((oledShowSpeed.load() - int(oledShowSpeed.load())) * 10);

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
        if (play)
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
        taskYIELD();
    }
}
void TaskMotor(void *pvParameters)
{
    TaskMotorParam *param = (TaskMotorParam *)pvParameters;
    int speed = param->speed, max = param->max, min = param->min;
    states state = param->state;
    delete param;

    if (state == stateToggle)
    {
        while (1)
        {
            Serial.printf("Running with speed: %d\n", speed);
            vTaskDelay(50 / portTICK_PERIOD_MS);
            taskYIELD();
        }
    }
    else if (state == stateShiftDown)
    {
        speedSetterMax.store(currentSpeed.load());
        oledShow.store(false);
        for (int i = max; i >= min; i--)
        {
            Serial.println(i);
            speedSetterMin.store(i);
            oledShowSpeed.store(i);
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
        oledShow.store(true);
        speedSetterMax.store(currentSpeed.load());
        speedSetterMin.store(stopSpeed.load());
        play.store(0);
    }
    else if (state == stateShiftUp)
    {
        speedSetterMin.store(stopSpeed.load());
        oledShow.store(false);
        for (int i = min; i <= max; i++)
        {
            Serial.println(i);
            speedSetterMax.store(i);
            oledShowSpeed.store(i);
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
        oledShow.store(true);
        speedSetterMax.store(currentSpeed.load());
        speedSetterMin.store(stopSpeed.load());
        play.store(1);
    }
    runTaskHandle = NULL;
    vTaskDelete(NULL);
}

void TaskButtons(void *pvParameters)
{
    (void)pvParameters;
    fio pinMode(buttonPins[i], INPUT_PULLUP);
    fio buttonPushed[i].store(0);
    play.store(0);
    shift.store(0);
    currentSpeed.store(0);
    rotaryCurrentSpeed.store(0);
    stopSpeed.store(rotMin);
    speedSetterMax.store(currentSpeed.load());
    speedSetterMin.store(stopSpeed.load());
    oledShow.store(true);
    bool UpDownFlag = false;
    while (1)
    {
        fio buttonPushed[i].store(!digitalRead(buttonPins[i]));
        if (buttonPushed[buttonShift].load() && buttonPushed[buttonToggle].load())
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            while (!digitalRead(buttonPins[buttonToggle]))
                vTaskDelay(50 / portTICK_PERIOD_MS);
            shift.store(!shift.load());
            if (runTaskHandle != NULL)
            {
                vTaskDelete(runTaskHandle);
                runTaskHandle = NULL;
            }
            TaskMotorParam *param = new TaskMotorParam;
            param->state = shift.load() ? stateShiftUp : stateShiftDown;
            speedSetterMax.store(currentSpeed.load());
            speedSetterMin.store(stopSpeed.load());
            param->max = speedSetterMax.load();
            param->min = speedSetterMin.load();
            xTaskCreatePinnedToCore(TaskMotor, "TaskMotor", 2048, param, 1, &runTaskHandle, ARDUINO_RUNNING_CORE);
        }
        else if (buttonPushed[buttonToggle].load())
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            while (!digitalRead(buttonPins[buttonToggle]))
                vTaskDelay(50 / portTICK_PERIOD_MS);
            play.store(!play.load());
            shift.store(!shift.load());
            if (runTaskHandle != NULL)
            {
                vTaskDelete(runTaskHandle);
                runTaskHandle = NULL;
            }
            TaskMotorParam *param = new TaskMotorParam;
            currentSpeed.store(rotaryCurrentSpeed.load());
            param->speed = play.load() ? currentSpeed.load() : stopSpeed.load();
            param->state = stateToggle;
            xTaskCreatePinnedToCore(TaskMotor, "TaskMotor", 2048, param, 1, &runTaskHandle, ARDUINO_RUNNING_CORE);
        }
        else if (buttonPushed[buttonFast].load())
        {
            int speed = currentSpeed.load();
            if (speed <= buttonUPMax)
            {
                speed++;
                currentSpeed.store(speed);
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
            UpDownFlag = true;
        }
        else if (buttonPushed[buttonSlow].load())
        {
            int speed = currentSpeed.load();
            if (speed >= buttonDownMin)
            {
                speed--;
                currentSpeed.store(speed);
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
            UpDownFlag = true;
        }
        else if (!buttonPushed[buttonFast].load() && !buttonPushed[buttonSlow].load() && UpDownFlag)
        {
            UpDownFlag = false;
            currentSpeed.store(rotaryCurrentSpeed.load());
        }
        taskYIELD();
    }
}
void TaskPrintScreen(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        fio Serial.print(buttonPushed[i].load());
        Serial.printf("\t play %d\tshift %d", play.load(), shift.load());
        Serial.println();
        taskYIELD();
    }
}
void setup()
{
    Serial.begin(9600);
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

    pinMode(CLK, INPUT);
    pinMode(DT, INPUT);
    attachInterrupt(digitalPinToInterrupt(CLK), updateEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(DT), updateEncoder, CHANGE);
    xTaskCreatePinnedToCore(TaskButtons, "TaskBTN", 1024, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
    xTaskCreatePinnedToCore(TaskUpdateScreen, "TaskScreen", 2048, NULL, 1, NULL, ARDUINO_RUNNING_CORE);

    // xTaskCreatePinnedToCore(TaskPrintScreen, "TaskPrint", 2048, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
}
void loop()
{
}