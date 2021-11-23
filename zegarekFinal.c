//                 //
//     ZEGAREK	   //
//	CHANAJ KAROL   //
//		2020	   //
//                 //

#include <8051.h>

#define TRUE 1
#define TH0ini 256 - 30
#define L960ini 960 % 256
#define H960ini 960 / 256 + 1
#define ENTER 1
#define ESC 2
#define RIGHT 4
#define UP 8
#define DOWN 16
#define LEFT 32

__bit __at (0x95) BUZZ;             //brzęczyk
__bit __at (0x96) DSP7;             //wyłącznik wyświetlacza 7-seg
__bit __at (0x97) TLED;             //led
__bit __at (0xB5) KEYB;             //klawisz klawiatury sekwencyjnej

__xdata unsigned char * DISP = (__xdata unsigned char * ) 0xFF30,	//wybór wyświetlacza
                        * SEGM = (__xdata unsigned char * ) 0xFF38;	//zatrzask do wyboru segmentów

int V960 = 960;

//wyświetlacz
unsigned char MASK = 1,			//"id" wyświetlacza
              INDX = 0,
              //klawisze
              keyMASK = 1,		//"id" klawisza
              keyINDX = 1,
              SKEY = 0,			//inicjuje obsługę klawiszy po pomyślnym teście na stabilność
              KEY = 0,			//klawisz który przeszedł test
              //aktualna godzina
              LS = 0,			//liczba sekund...
              LM = 0,			//minut...
              LG = 0,			//i godzin
              //tryb nastawiania godziny
              blinkSave = 0,	//zapisuje zmieniany wyświetlacz
              blinkPart = 0,	//przechowuje aktualny stan mrugania - przyjmuje wartości 1,4,16 dla sekund, minut i godzin jeśli mają być zgaszone
              //										lub 0 jeśli mają znowu zostać zapalone
              changeTime = 0,	//przechowuje informację czy tryb nastawiania godziny jest aktywny
              //alarm
              alarmLG = 0,		//godzina
              alarmLM = 0,		//i minuta alarmu
              alarmON = 0,		//czy alarm aktualnie dzwoni
              alarmIsSet = 0,	//czy alarm jest nastawiony
              alarmChangeMode = 0,	//tryb przestawienia godziny alarmu
              //tablice
              KEYBD[4] = {0,0,0,0},	//tablica do stabilizacji klawiatury
                         CYFRY[] = { 0b00111111, 0b00000110, 0b01011011, 0b01001111,	//wzory na wyświetlaczu
                                     0b01100110, 0b01101101, 0b01111101, 0b00000111,
                                     0b01111111, 0b01101111
                                   };

void TIMER0() __interrupt(1) {	//obsługa przerwania
    TH0 = TH0ini;	//żeby następne przerwanie nastąpiło po 1/960 s
    F0 = 1;			//informujemy pętlę główną o wystąpieniu przerwania
}

void prep_env() {	//przygotowanie środowiska
    IE = 0; 		//włączamy wszystkie przerwania
    TMOD = 0x70;	//blokada TIMER1, TIMER0 w trybie 16-bitowym

    TH0 = TH0ini;	//żeby pierwsze przerwanie nastąpiło po 1/960 s
    F0 = 0;			//na wszelki wypadek

    TCON = 0x10;    //zgoda na zliczanie przez TIMER0
    ET0 = 1; 		//zgoda na obsługę przerwań od TIMER0
    EA = 1;         //globalna zgoda na obsługę (wszystkich) przerwań

    TLED = 0;		//początkowy stan LEDa
}

void sel_segm() {	//wybranie segmentów dla wyświetlacza
    if(MASK == 1)				//w zależności od aktualnie wybranego wyświetlacza
        *SEGM = CYFRY[LS%10];	//wybieram odpowiednią liczbę do wyświetlenia
    else if(MASK == 2)			//...
        *SEGM = CYFRY[LS/10];
    else if(MASK == 4)
        *SEGM = CYFRY[LM%10];
    else if(MASK == 8)
        *SEGM = CYFRY[LM/10];
    else if(MASK == 16)
        *SEGM = CYFRY[LG%10];
    else if(MASK == 32)
        *SEGM = CYFRY[LG/10];
    else if(MASK == 64 && alarmIsSet)	//zapalenie F1 przy nastawionym budziku
        *SEGM = 0b00000001;
    else						//zgaszenie reszty
        *SEGM = 0;
}

void selAlarmSegm() {	//wybranie segmentów dla alarmu
    if(MASK == 1)
        *SEGM = 0;
    else if(MASK == 2)
        *SEGM = 0;
    else if(MASK == 4)
        *SEGM = CYFRY[alarmLM%10];
    else if(MASK == 8)
        *SEGM = CYFRY[alarmLM/10];
    else if(MASK == 16)
        *SEGM = CYFRY[alarmLG%10];
    else if(MASK == 32)
        *SEGM = CYFRY[alarmLG/10];
    else if(MASK == 64 && alarmIsSet)
        *SEGM = 0b00000001;
    else {
        *SEGM = 0;
    }
}

void resetKEY() {	//resetuje tablicę do stabilizacji klawiatury
    KEYBD[0] = 0;
    KEYBD[1] = 0;
    KEYBD[2] = 0;
    KEYBD[3] = 0;
}

void changeTimeMode() {		//przełącza tryb ustawiania czasu
    if(!changeTime) {
        changeTime = 1;		//włączenie trybu
        blinkPart = 1;		//miganie sekund
        blinkSave = 1;		//zapisanie, że migają sekundy
    }
    else {
        changeTime = 0;		//wyłączenie trybu
        blinkPart = 0;		//ew. zapalenie wyświetlacza
        V960 = 960;			//reset licznika
    }
}

void blink() {				//funkcja przełączająca zmienną blinkPart
    if(blinkPart != 0)		//między wartościami blinkSave i 0
        blinkPart = 0;
    else
        blinkPart = blinkSave;
}

void incHOUR(unsigned char mode) {		//funkcja do inkrementacji godzin
    if(alarmChangeMode && mode) {		//jeśli jest włączony tryb ustawiania alarmu i funkcja została wywołana klawiszem
        alarmLG++;
        if(alarmLG >= 24) {
            alarmLG = 0;
        }
    }									//mode pozwala na inkrementację godzin "w tle" podczas nastawiania alarmu
    else {
        LG++;
        if(LG >= 24) {
            LG = 0;
        }
    }
}

void incMIN(unsigned char mode) {		//funkcja do inkrementacji minut
    if(alarmChangeMode && mode) {		//tak samo jak z godzinami
        alarmLM++;
        if(alarmLM >= 60) {
            alarmLM = 0;
        }
    }
    else {
        LM++;
        if(LM >= 60) {
            LM = 0;
            if(!changeTime)				//nie inkrementuje godzin w trybie nastawiania godziny
                incHOUR(0);
        }
    }
}

void incSEC() {		////funkcja do inkrementacji sekund
    LS++;
    if(LS >= 60) {
        LS = 0;
        if(!changeTime)	//nie inkrementuje minut w trybie nastawiania
            incMIN(0);
    }
}

void incTime(unsigned char mode) {				//zmiana godziny "w górę"
    if(blinkSave == 1) {		//sprawdzenie zmienianego wyświetlacza
        incSEC();			//wykorzystanie funkcji używanej przy normalnej pracy zegarka
    }
    else if(blinkSave == 4) {
        incMIN(mode);
    }
    else if(blinkSave == 16) {
        incHOUR(mode);
    }
}

void decTime() {				//zmiana godziny "w dół"
    if(changeTime) {
        if(blinkSave == 1) {		//sprawdzenie wyświetlacza
            if(LS > 0)			//dekrementacja
                LS--;
            else				//lub zapętlenie dla zera
                LS = 59;
        }
        else if(blinkSave == 4) {
            if(LM > 0)
                LM--;
            else
                LM = 59;
        }
        else if(blinkSave == 16) {
            if(LG > 0)
                LG--;
            else
                LG = 23;
        }
    }
    else {						//wersja dla przestawiania budzika
        if(blinkSave == 4) {
            if(alarmLM > 0)
                alarmLM--;
            else
                alarmLM = 59;
        }
        else if(blinkSave == 16) {
            if(alarmLG > 0)
                alarmLG--;
            else
                alarmLG = 23;
        }
    }
}

void setAlarm() {			//przełącza tryb nastawiania alarmu
    if(!alarmChangeMode) {
        alarmChangeMode = 1;//włączenie trybu
        blinkPart = 4;		//miganie minut
        blinkSave = 4;		//zapisanie, że migają minuty
    }
    else {
        alarmChangeMode = 0;		//wyłączenie trybu
        blinkPart = 0;		//ew. zapalenie wyświetlacza
    }
}

void checkAlarm() {			//sprawdzenie czy godzina alarmu = aktualna godzina
    if(alarmIsSet && !alarmChangeMode &&
            LM == alarmLM &&
            LG == alarmLG) {
        alarmON = 1;	//alarm będzie dzwonił
    }

}

void show_time() {			//pokaż czas
    DSP7 = 1;				//wyłączam wyświetlacze
    *DISP = MASK;           //wybieram wyświetlacze

    //sprawdzenie czy dany wyświetlacz nie musi zostać wyłączony na potrzeby "mrugania" przy nastawianiu
    //		jednostki				dziesiątki
    if(MASK != blinkPart && MASK != (blinkPart << 1)) {
        if(alarmChangeMode)
            selAlarmSegm();	//wyświetlanie dla nastawiania alarmu
        else
            sel_segm();		//normalne wyświetlanie godziny
    }
    else
        *SEGM = 0;			//aby wyświetlacz był zgaszony nie może mieć żadnych segmentów
    DSP7 = 0;				//wyłączam wyświetlacze

    if( KEYB )
        KEYBD[0] |= MASK;

    MASK <<= 1;		//przesunięcie wyświetlacza na następny w lewo
    INDX++;			//inkrementacja "numeru" wyświetlacza
    SKEY = 0;		//reset testu na stabilność
    if( INDX >= 7 ) {
        DSP7 = 1;					//wyłączenie wyświetlacza
        *DISP = keyMASK;			//załadowanie klawisza
        if(T1) {						//sprawdzenie czy jest wciśnięty
            KEYBD[3] = KEYBD[2];	//dodanie go do tablicy
            KEYBD[2] = KEYBD[1];
            KEYBD[1] = KEYBD[0];
            KEYBD[0] = keyMASK;
            if( KEYBD [3] != KEYBD[2] &&	//test stabilności klawiatury
                    KEYBD [2] == KEYBD[1] &&
                    KEYBD [1] == KEYBD[0] ) {
                SKEY = 1;			//pomyślny test na stabilność
                KEY = keyMASK;		//zapamiętanie klawisza, który przeszedł test
            }
        }
        else {
            if(keyMASK == KEY) {			//po puszczeniu wciśniętego klawisza, następuje reset
                resetKEY();
                KEY = 0;
            }
        }
        keyMASK <<= 1;				//przesunięcie na kolejny klawisz
        keyINDX++;					//inkrementacja indeksu klawisza
        if(keyINDX > 6) {			//ew. powrót na początek
            keyMASK = 1;
            keyINDX = 0;
        }

        if(MASK != blinkPart && MASK != (blinkPart << 1)) {
            if(alarmChangeMode)
                selAlarmSegm();	//wyświetlanie dla nastawiania alarmu
            else
                sel_segm();		//normalne wyświetlanie godziny
        }
        else
            *SEGM = 0;
        DSP7 = 0;					//ponowne włączenie wyświetlaczy
        MASK = 1;					//powrót na pierwszy wyświetlacz
        INDX = 0;
    }

}

void alarmOFF() {					//wyłączenie dzwoniącego alarmu
    if(!changeTime && !alarmChangeMode && alarmON) {
        alarmON = 0;
        alarmIsSet = 0;
        BUZZ = 1;
    }
}

void key_serv() {					//obsługa klawiszy
    if(KEY == ENTER) {
        if(!alarmChangeMode)
            changeTimeMode();		//włączenie / wyłączenie nastawiania
        else {
            alarmIsSet = !alarmIsSet;
        }
    }
    else if(KEY == ESC) {
        alarmOFF();		//ew. wyłączenie alarmu
        if(!changeTime)				//tryb nastawiania alarmu
            setAlarm();
    }
    else if(KEY == RIGHT) {
        alarmOFF();		//ew. wyłączenie alarmu
        if(changeTime && blinkSave > 1) {	//przesunięcie nastawiania w prawo
            blinkSave >>= 2;
        }
        else if (alarmChangeMode && blinkSave > 4) {	//przesunięcie nastawiania w prawo dla alarmu (nie przesuwa na sekundy)
            blinkSave >>= 2;
        }
        else {}
    }
    else if(KEY == LEFT) {
        alarmOFF();
        if(blinkSave < 16)	//przesunięcie nastawiania w lewo
            blinkSave <<= 2;
    }
    else if(KEY == UP) {
        alarmOFF();
        if(changeTime || alarmChangeMode)		//zmiana godziny w górę
            incTime(1);
    }
    else if(KEY == DOWN) {
        alarmOFF();
        if(changeTime || alarmChangeMode)		//zmiana godziny w dół
            decTime();
    }
}

void run_clock() {			//zegarek
    while( TRUE ) {
        while(!F0); 	//czekam na przerwanie
        F0 = 0;			//zapominam o przerwaniu

        show_time();	//wyświetlenie godziny (+sprawdzenie klawiszy)
        if(SKEY)		//klawiatura stabilna i nie pusta
            key_serv(); //obsługa klawiszy

        if(--V960 == 959 || V960 == 479) {
            //funkcje wykonywane 2/s
            if(changeTime || alarmChangeMode)
                blink();	//mruganie wyświetlaczem
            if(alarmON)
                BUZZ = !BUZZ;
        }

        if(V960) continue;

        V960 = 960;	//reset

        if(!changeTime) {
            TLED = !TLED;	//mruganie LEDem 1/s
            incSEC();		//inkrementacja czasu
        }
        checkAlarm();	//sprawdzenie alarmu
    }
}

int main() {
    prep_env();		//przygotowanie środowiska
    run_clock();	//uruchomienie zegarka
    return 0;
}