# Sistem Autonom de Scanare si Avertizare (Radar Ultrasonic)

## Introducere
Proiectul consta in realizarea unui **sistem inteligent de tip radar / turela defensiva**, capabil sa scaneze mediul inconjurator la 180 de grade si sa detecteze obstacole in timp real. 

**Elementul de noutate si independenta sistemului:**
Spre deosebire de majoritatea proiectelor de tip radar care folosesc mediul Arduino si o conexiune seriala (Processing/Python) pentru a afisa datele pe un PC, **acest sistem este 100% independent ("Stand-alone")**. Toate calculele trigonometrice, generarea buffer-ului video si deciziile de alertare au loc intern pe microcontrolerul ATmega328P. 

In momentul in care un obiect patrunde in perimetrul critic (< 10 cm), sistemul intrerupe scanarea, blocheaza mecanic senzorul pe unghiul tintei si declanseaza instantaneu sistemul de alarma (Buzzer + LED), pastrand punctul pe ecranul OLED ca o "memorie spatiala".

---

## Hardware Design

**Lista de piese (BOM - Bill of Materials)**

| Componenta | Pin MCU | Rol / Justificare |
| :--- | :--- | :--- |
| **ATMEGA328P-XMINI** | - | Microcontroler - Creierul sistemului care coordoneaza perifericele. |
| **HC-SR04+** | Trig: `PD2`, Echo: `PD3` | Senzor distanta - GPIO digital pentru declansare si citire ecou. |
| **SG90 Servo** | `PB1 (OC1A)` | Servomotor - Miscare de maturare actionata prin semnal PWM hardware. |
| **Display OLED 0.96"** | SDA: `PC4`, SCL: `PC5` | Afisaj grafic - Protocol I2C hardware (TWI) pentru ecran standalone. |
| **Buzzer Activ 5V** | `PC3` | Alarma sonora - GPIO digital (Active LOW). |
| **LED RGB** | `PC0`, `PC1`, `PC2` | Alarma vizuala - Semnalizare stari (Rosu/Verde) cu rezistente de 330 Ohm. |

> *(Am utilizat un LM7805 in schema electrica pentru cazul extinderii la alimentare pe baterie, insa momentan sistemul se alimenteaza stabilizat din portul USB 5V).*

---

## Software Design

**Mediu de dezvoltare:** PLATFORM IO (C/C++ Baremetal)

**Motivarea bibliotecilor (De ce Baremetal?):**
Proiectul a fost implementat **fara** biblioteci externe de nivel inalt (precum `Adafruit_GFX` sau `Servo.h`). S-a utilizat exclusiv `avr/io.h` pentru lucrul direct cu registrii.
* **Motivare:** Controlul total asupra timing-ului. Citirea corecta a impulsului ultrasonic necesita microsecunde, iar scrierea a 1024 de octeti pe OLED prin I2C necesita o magistrala de date neintrerupta de functii high-level care ar putea adauga latente.

**Sinergia functionalitatilor studiate:**
1. **GPIO:** Utilizat pentru aprinderea LED-urilor, comanda de baza a Buzzer-ului si emiterea semnalului TRIG.
2. **Timere & PWM:**
   * **Timer 1** (Fast PWM pe 16 biti): Generarea frecventei de 50Hz (`ICR1=4999`) cu un prescaler de 64 pentru controlul mecanic al Servomotorului.
   * **Timer 0**: Cronometrarea duratei impulsului ECHO primit de la HC-SR04.
3. **Comunicatia I2C:** Manipularea hardware a registrelor TWI (`TWBR`, `TWCR`, `TWDR`) pentru a controla ecranul SSD1306 la o frecventa de 100kHz.

**Scheletul Proiectului si Modul de Interactiune (Super Loop):**
Logica a fost structurata intr-un `while(1)` non-blocant, bazat pe stari dinamice:
* **Pasul 1 (Sincronizare Mecanica):** Se calculeaza indexul unghiului curent (0-18) din registrul PWM.
* **Pasul 2 (Senzoristica):** Se apeleaza `get_distance()`, care lanseaza ping-ul.
* **Pasul 3 (Mapare):** Distanta calculata (0-100cm) este salvata in array-ul `radar_map`, formand memoria temporala a scanarii.
* **Pasul 4 (Automata - Decizie):** * Daca **D <= 10cm** ➔ Starea de **ALARMA**. Se blocheaza incrementarea unghiului (Timer1 fix), se aprinde rosu si porneste buzzerul. 
  * Daca **D > 10cm** ➔ Starea de **SCANARE**. Unghiul este incrementat (sweep stanga-dreapta), se mentine LED-ul verde.
* **Pasul 5 (Afisare Grafica):** Se genereaza Frame Buffer-ul (1KB). Baza radarului, punctele mapate cu raze proportionale din `radar_map`, si linia de scanare desenata utilizand algoritmul lui Bresenham. Cadrul final este "turnat" (flushed) pe I2C.

---

## Elementul de Noutate: Motorul Grafic 2D (OLED)

Transformarea display-ului SSD1306 dintr-un simplu afisaj de text intr-un sistem capabil sa randeze grafica dinamica 2D in timp real a reprezentat nucleul inovatiei acestui proiect. Acest motor grafic se bazeaza pe 4 piloni tehnici:

1. **Conceptul de Frame Buffer (1KB RAM):** Display-urile OLED standard nu permit desenarea pixelilor individuali fara a rescrie pagina (flickering). Solutia a fost alocarea a jumatate din SRAM-ul microcontrolerului (1024 octeti) pentru o "harta virtuala". Cadrul e compus in memorie, apoi trimis integral pe I2C.
2. **Trigonometrie Scalata (LUT):** Deoarece ATmega328P nu are unitate hardware Floating-Point (FPU), s-a implementat un Look-Up Table (LUT) precalculat pentru `sin()` si `cos()`. Valorile au fost scalate cu 64, aducand calculele in domeniul intreg (int16_t) folosind shiftarea pe biti (`>> 6`).
3. **Algoritmul lui Bresenham:** Pentru a trasa linia radarului de la baza ecranului, s-a implementat algoritmul lui Bresenham, folosind exclusiv adunari si scaderi.
4. **Memoria Spatiala:** Sistemul pastreaza un array de 19 elemente ce stocheaza distantele detectate la fiecare unghi. Obiectele sunt randate ca "blip-uri" (2x2 pixeli), iar cand un obiect paraseste zona, acesta dispare vizual din urmatorul frame.

---

## Optimizari si Calibrari

* **Optimizarea I2C (Burst Mode):** Modificarea functiei `oled_flush()` pentru a trimite un singur semnal de Start si Control Byte (`0x40`), varsand tot array-ul de 1024 octeti continuu, reducand overhead-ul magistralei masiv.
* **Prevenirea Overflow-ului Matematic:** Fortarea tipului `int16_t` in calculele trigonometrice (Bresenham) pentru a evita depasirea limitei de 127 a variabilelor pe 8 biti.
* **Calibrarea Senzorului HC-SR04:** Timpul mort setat la 100ms pentru a evita ecourile fantoma. Utilizarea unui prescaler de 64, simplificand extragerea distantei la formula directa: `ticks / 14`.

---

## Rezultate Obtinute / Demo

Sistemul a fost testat cu succes, respectand parametrii planificati. Radarul "vede" si memoreaza corect obiecte pe o raza de 1 metru pe ecran si reactioneaza instantaneu (blocare mecanica) la un perimetru critic de 10cm.

## Dezvoltari Ulterioare si Utilitate Practica

Sistemul poate fi extins usor pentru aplicatii reale din industria de securitate sau robotica:
* **Conectivitate IoT:** Adaugarea unui modul ESP8266 pentru a transmite "harta" radarului in timp real pe un server.
* **Sensor Fusion:** Integrarea unui senzor PIR (Infrarosu) pentru a diferentia intre un obstacol fizic si un intrus viu (sursa de caldura).
* **Utilitate Practica:** Arhitectura "Stand-alone" il face ideal ca modul independent de evitare a coliziunilor (Blind-Spot Monitor) pentru roboti autonomi sau ca mini-sistem de siguranta industriala (oprirea utilajelor la incalcarea perimetrului).
