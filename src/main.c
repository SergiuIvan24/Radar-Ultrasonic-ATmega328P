#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
#include <math.h>

#define OLED_ADDR 0x78

uint8_t buffer[1024];
uint8_t radar_map[19] = {0};

const int8_t SIN_LUT[] = {0, 11, 21, 32, 41, 49, 55, 60, 63, 64, 63, 60, 55, 49, 41, 32, 21, 11, 0};
const int8_t COS_LUT[] = {64, 63, 60, 55, 49, 41, 32, 21, 11, 0, -11, -21, -32, -41, -49, -55, -60, -63, -64};

void i2c_init() { TWSR = 0x00; TWBR = 72; TWCR = (1 << TWEN); }
void i2c_start() { TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN); while (!(TWCR & (1 << TWINT))); }
void i2c_stop() { TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN); }
void i2c_write(uint8_t data) { TWDR = data; TWCR = (1 << TWINT) | (1 << TWEN); while (!(TWCR & (1 << TWINT))); }

void oled_command(uint8_t cmd) { i2c_start(); i2c_write(OLED_ADDR); i2c_write(0x00); i2c_write(cmd); i2c_stop(); }

void oled_flush() {
    oled_command(0x21); oled_command(0); oled_command(127);
    oled_command(0x22); oled_command(0); oled_command(7);
    i2c_start();
    i2c_write(OLED_ADDR);
    i2c_write(0x40);
    for (uint16_t i = 0; i < 1024; i++) {
        i2c_write(buffer[i]);
    }
    i2c_stop();
}

void oled_clear_buffer() {
    for (uint16_t i = 0; i < 1024; i++) buffer[i] = 0;
}

void oled_set_pixel(int8_t x, int8_t y, uint8_t color) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    if (color) buffer[x + (y / 8) * 128] |= (1 << (y % 8));
    else       buffer[x + (y / 8) * 128] &= ~(1 << (y % 8));
}

//alg lui bresenham pt linie
void oled_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy, e2;
    while (1) {
        oled_set_pixel(x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void init_sonar() { DDRD |= (1 << PD2); DDRD &= ~(1 << PD3); } // PD2 output(trig), PD3 input(echo)

uint16_t get_distance() {
    // trimit puls de 10 us pe trig
    PORTD &= ~(1 << PD2); _delay_us(2);
    PORTD |= (1 << PD2);  _delay_us(10);
    PORTD &= ~(1 << PD2);

    uint16_t timeout = 10000;
    while (!(PIND & (1 << PD3))) { if (--timeout == 0) return 0; } //astept sa inceapa ecoul

    // pornesc timer pt a masura data
    TCCR0B = (1 << CS01) | (1 << CS00);
    TCNT0 = 0;
    uint16_t ticks = 0;

    while (PIND & (1 << PD3)) {
        if (TCNT0 >= 200) {
            ticks += TCNT0;
            TCNT0 = 0;
        }
        if (ticks > 6000) break; //limita maxima de distanta
    }
    ticks += TCNT0;
    TCCR0B = 0;

    return ticks / 14;
}

void init_pwm_servo() {
    DDRB |= (1 << PB1);
    TCCR1A = (1 << COM1A1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11) | (1 << CS10);
    ICR1 = 4999;
}

int main(void) {
    i2c_init();
    oled_command(0xAE); oled_command(0x20); oled_command(0x00);
    oled_command(0x8D); oled_command(0x14); oled_command(0xAF);
    init_sonar();
    init_pwm_servo();
    //configurare pini pt led si buzzer
    DDRC |= (1 << PC0) | (1 << PC1) | (1 << PC3);
    PORTC |= (1 << PC3);

    uint16_t servo_pwm = 250; // poz de start motor
    int8_t step = 14;         // viteza motor
    int8_t raza_ecran = 55;   // lungime linie radar pe ecran

    while (1) {
        uint8_t angle_idx = (servo_pwm - 250) / 14;
        if (angle_idx > 18) angle_idx = 18;
        uint16_t dist = get_distance();

        radar_map[angle_idx] = 0;
        if (dist > 0 && dist <= 100) {
            radar_map[angle_idx] = (uint8_t)dist;
        }

        if (dist > 0 && dist <= 10) {
            //zona alarma
            PORTC |= (1 << PC0);
            PORTC &= ~(1 << PC1);
            PORTC &= ~(1 << PC3);
        } else {
            //zona safe
            PORTC &= ~(1 << PC0);
            PORTC |= (1 << PC1);
            PORTC |= (1 << PC3);

            servo_pwm += step;

            if (servo_pwm >= 500 || servo_pwm <= 250) step = -step;
        }

        OCR1A = servo_pwm;

        _delay_ms(100);

        oled_clear_buffer();

        for (int x = 40; x < 88; x++) oled_set_pixel(x, 60, 1);

        for (uint8_t i = 0; i <= 18; i++) {
            uint8_t d = radar_map[i];
            if (d > 0 && d <= 100) {
                int16_t r_target = ((int16_t)d * raza_ecran) / 100;
                int16_t xt = 64 + (r_target * (int16_t)COS_LUT[i]) / 64;
                int16_t yt = 60 - (r_target * (int16_t)SIN_LUT[i]) / 64;

                oled_set_pixel((int8_t)xt, (int8_t)yt, 1);
                oled_set_pixel((int8_t)(xt+1), (int8_t)yt, 1);
                oled_set_pixel((int8_t)xt, (int8_t)(yt+1), 1);
                oled_set_pixel((int8_t)(xt+1), (int8_t)(yt+1), 1);
            }
        }

        int16_t x1 = 64 + ((int16_t)raza_ecran * (int16_t)COS_LUT[angle_idx]) / 64;
        int16_t y1 = 60 - ((int16_t)raza_ecran * (int16_t)SIN_LUT[angle_idx]) / 64;

        oled_draw_line(64, 60, x1, y1);

        oled_flush();
    }
}