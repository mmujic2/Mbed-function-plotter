#include "mbed.h"
#include "stm32f413h_discovery_ts.h"
#include "stm32f413h_discovery_lcd.h"

TS_StateTypeDef TS_State = { 0 };

#define yellow 0xE6A0
#define lightBlue 0x44FF
#define dayTextColor 0x0000
#define dayBackground 0XFFFF
#define dayPlotBackground 0xAD55
#define nightPlotBackground 0x19AF
#define nightBackground 0x0083
#define nightTextColor 0xDEDB
#define detailsColor  0xFAC0
#define testcolor 0xF800
#define eps 0.0001
#define numberOfFunctions 8
#define numberOfModes 4

Point moon[12];
int background = dayBackground;
int plotBackground = dayPlotBackground;
int textColor = dayTextColor;

// Maxparametar = topvalue + botvalue
// Minparametar = botvalue
// max vrijednost za predefinisane funkcije (osim eksponencijalne) = maxAmplitude + maxOffset
// min vrijednost = minAmplitude - minOffset
// USLOVI:
// max > min
// minfrekvencija treba biti > 0,
// apsolutna vrijednost od max i min vrijednosti ne bi trebala biti veca od 9.9 (ovo
// vrijedi i za korisnicki definisane funkcije)
#define topAmplitude 2.5
#define botAmplitude 0.5
#define topOffset 1
#define botOffset 0
#define topFrequency 3.0
#define botFrequency 1.0
#define topSamples 30
#define botSamples 30
double maxSamples = topSamples + botSamples;
double *samples = new double[topSamples + botSamples];

const double PI = atan(1) * 4;
const int lcdW = BSP_LCD_GetXSize();
const int lcdH = BSP_LCD_GetYSize();

double oldaxValues[11]; // potrebno pri brisanju starih vrijednosti
double axValues[11];  // sadrzi vrijednosti po y-osi
const int ayPositions[] = {45, 61, 77, 93, 109, 125, 141, 157, 173, 189, 205}; // Pozicije isrtvanja brojeva na y-osi
double maxValue = 0.0; // potrebno pri reskaliranju vrijednosti na y-osi
int T = 0; // trenutno vrijeme
int numberOfSamples = 0;
double drawSpeed = 0.5;

double ayoffset = 0;
double axoffset = 0;
double amplitude = 0.0;
double frequency = 1.0;

char function = 0;
char mode = 2;
char modeClicks = 0;
char signalClicks = 0;

bool setup = true; // potrebno pri pokretanju programa
bool refresh = false;
bool staticDrawing = false;
bool dayTheme = true;
bool details = false;

AnalogIn potentSamples(p15);
AnalogIn potentAmplitude(p16);
AnalogIn potentayOffset(p17);
AnalogIn potentaxOffset(p18);
AnalogIn potentFrequency(p19);
AnalogIn potentSpeed(p20);

InterruptIn buttonSet(p5);
InterruptIn buttonStatic(p6);
InterruptIn buttonSignal(p7);
InterruptIn buttonMode(p8);
InterruptIn buttonStopDraw(p9);
InterruptIn buttonTheme(p10);

Ticker t1; // tiker koji kontrolise crtanje

void reDrawSamples();
void resetParameters();
void changeLimit();
void staticDraw();
void dynamicDraw();
void drawSample();
void appendSignalName(char var, char* text);
void appendModeName(char var, char* text);
void stopDraw();
void startDraw();

// funkcija za ponovno ucitavanje displeja
void reinitialize() {
    BSP_LCD_Clear(background);
    BSP_LCD_SetBackColor(background);
    
    BSP_LCD_SetTextColor(plotBackground);
    BSP_LCD_FillRect(35, 45, 205, 170);
    
    BSP_LCD_SetTextColor(textColor);
    BSP_LCD_DrawHLine(0, 40, BSP_LCD_GetXSize());
    BSP_LCD_DrawHLine(35, 130, BSP_LCD_GetXSize() - 40);
    BSP_LCD_DrawVLine(40, 50, BSP_LCD_GetYSize() - 80);
    maxValue = -9.9;
    numberOfSamples = 0;
    // changeLimit();
    resetParameters();
    if(dayTheme && !details) {
        // iscrtavanje mjeseca
        BSP_LCD_SetTextColor(nightBackground);
        BSP_LCD_FillRect(220, 220, 20, 20);
        BSP_LCD_SetTextColor(yellow);
        BSP_LCD_FillPolygon(moon, 12);
        BSP_LCD_SetTextColor(textColor);
    }
    else if(!details){
        // iscrtavanje sunca
        BSP_LCD_SetTextColor(lightBlue);
        BSP_LCD_FillRect(220, 220, 20, 20);
        BSP_LCD_SetTextColor(yellow);
        BSP_LCD_FillCircle(230, 230, 5);
        BSP_LCD_SetTextColor(textColor);
    }
}

// funkcija koja kontrolise temu
void changeTheme() {
    if(dayTheme) {
        dayTheme = false;
        background = nightBackground;
        plotBackground = nightPlotBackground;
        textColor = nightTextColor;
        reinitialize();
    }
    else {
        dayTheme = true;
        background = dayBackground;
        plotBackground = dayPlotBackground;
        textColor = dayTextColor;
        reinitialize();
    }
}

// funkcija koja kntrolise prikaz detalja
void showDetails() {
    if(!details) {
        // ne dozvoljavama sljedece funkcionalnosti kada su detalji prikazani
        buttonStopDraw.fall(NULL);
        buttonStatic.rise(NULL);
        buttonStatic.fall(NULL);
        buttonTheme.fall(NULL);
        
        details = true;
        t1.detach();
        
        BSP_LCD_Clear(background);
        BSP_LCD_SetTextColor(textColor);
        
        char temp[40];
        sprintf(temp, "Signal: ");
        appendSignalName(function, temp);
        BSP_LCD_DisplayStringAtLine(0, (uint8_t *) temp);
        
        sprintf(temp, "Mode: ");
        appendModeName(mode, temp);
        BSP_LCD_DisplayStringAtLine(1, (uint8_t *) temp);
        
        sprintf(temp, "Time: %d", T);
        BSP_LCD_DisplayStringAtLine(2, (uint8_t *) temp);
        
        sprintf(temp, "Number of samples: %d", numberOfSamples);
        BSP_LCD_DisplayStringAtLine(3, (uint8_t *) temp);
        
        double A = amplitude * topAmplitude + botAmplitude;
        sprintf(temp, "Amplitude coefficient: %.2f", A);
        BSP_LCD_DisplayStringAtLine(4, (uint8_t *) (uint8_t *) temp);
        
        double c1 = ayoffset * topOffset + botOffset;
        sprintf(temp, "x axis offset: %.2f", c1);
        BSP_LCD_DisplayStringAtLine(5, (uint8_t *) temp);
        
        double c2 = axoffset * topOffset - botOffset * PI;
        sprintf(temp, "y axis offset: %.2f pi", c2);
        BSP_LCD_DisplayStringAtLine(6, (uint8_t *) temp);
        
        double f = frequency * topFrequency + botFrequency;
        sprintf(temp, "frequency coefficient: %.2f", f);
        BSP_LCD_DisplayStringAtLine(7, (uint8_t *) temp);
        
        sprintf(temp, "Draw speed: %.2f s per sample", drawSpeed);
        BSP_LCD_DisplayStringAtLine(8, (uint8_t *) temp);
        
        BSP_LCD_SetTextColor(detailsColor);
        sprintf(temp, "Back");
        BSP_LCD_DisplayStringAtLine(9, (uint8_t *) temp);
        BSP_LCD_SetTextColor(textColor);
    }
    else {
        // Povratak mogucnosti nakon vracanja na graf
        details = false;
        if(!staticDrawing) {
            buttonStatic.rise(callback(&staticDraw));
        }
        else {
            buttonStatic.rise(callback(&dynamicDraw));
        }
        buttonStopDraw.fall(callback(&stopDraw));
        buttonTheme.fall(callback(&changeTheme));
        reinitialize();
    }
}

// funkcija koja kontrolise tip iscrtavanja
void drawPoint(int x1, int y1, int x2, int y2) {
    switch(mode) {
        case 0:
            if(y2 < 0) {
                break;
            }
            BSP_LCD_DrawLine(x1, y1, x2, y2);
        break;
        
        case 1: 
            if(y2 < 0) {
                break;
            }
            BSP_LCD_DrawLine(x1, y1, x1, y2);
            BSP_LCD_DrawLine(x1, y2, x2, y2);
        break;
        
        case 2:
            BSP_LCD_FillCircle(x1, y1, 2);
        break;
        
        case 3:
            BSP_LCD_FillCircle(x1, y1, 2);
            BSP_LCD_DrawLine(x1, 130, x1, y1);
        break;
    }
}

// Funkcija koja kontrolise opseg y-ose
void changeLimit() {
    for(int i(0); i < 11; i++) {
        if(!setup) {
            oldaxValues[i] = axValues[i];
        }
        // (maxValue * 1.5 + 1) - racuna opseg, ne dozvoljava se manje od 1.
        // maxValue moze biti izmedu 0 - 1
        axValues[i] = (maxValue * 1.5 + 1) * ((i / 10. - 0.5) * 2) * -1;
        if(setup) {
            oldaxValues[i] = axValues[i];
        }
        if(axValues[i] < eps && axValues[i] > -eps) {
            axValues[i] = 0.0;
        }
        char temp[5] = "";
        sprintf(temp, "%.1f", axValues[i]);
        int offset = 0;
        // Ukoliko broj nije negativan, treba poravnati zbog nedostajuceg minusa
        if(axValues[i] >= 0) {
            offset = 8;
        }
        BSP_LCD_DisplayStringAt(offset, ayPositions[i], (uint8_t *) temp, LEFT_MODE);
        BSP_LCD_DrawHLine(33, ayPositions[i] + 5, 2);
    }
}

// funkcija koja kontrolise pritisak dugmeta za promjenu signala
void incrementSignal() {
    signalClicks++;
    BSP_LCD_ClearStringLine(0);
    char temp[40] = "Signal: ";
    appendSignalName(function, temp);
    strcat(temp, "(");
    appendSignalName((signalClicks + function) % numberOfFunctions, temp);
    strcat(temp, ")");
    BSP_LCD_DisplayStringAtLine(0, (uint8_t *) temp);
}

// funkcija koja kontrolise pritisak dugmeta za promjenu tipa iscrtavanja
void incrementMode() {
    modeClicks++;
    BSP_LCD_ClearStringLine(1);
    char temp[40] = "Mode: ";
    appendModeName(mode, temp);
    strcat(temp, "(");
    appendModeName((modeClicks + mode) % numberOfModes, temp);
    strcat(temp, ")");
    BSP_LCD_DisplayStringAtLine(1, (uint8_t *) temp);
}

void stopDraw();
void drawSample();

// (f1, f2) - funkcije koje kontrolisu zaustavljanje i pokretanje crtanja
//f1
void startDraw() {
    t1.attach(callback(&drawSample), drawSpeed);
    wait_ms(500);
    buttonStopDraw.fall(callback(&stopDraw));
}
//f2
void stopDraw() {
    t1.detach();
    wait_ms(500);
    buttonStopDraw.fall(callback(&startDraw));
}

void staticDraw();

// funkcija koja kontrolise ispis trenutnog signala
void appendSignalName(char var, char* text) {
    switch(var) {
        case 0:
            strcat(text, "sine");
            break;
        case 1:
            strcat(text, "cosine");
            break;
        case 2:
            strcat(text, "sinc");
            break;
        case 3:
            strcat(text, "square");
            break;
        case 4:
            strcat(text, "sawtooth");
            break;
        case 5:
            strcat(text, "triangle");
            break;
        case 6:
            strcat(text, "exponential");
            break;
        case 7:
            strcat(text, "custom");
    }
}

// funkcija koja kontrolise ispis trenutnog tipa iscrtavnja
void appendModeName(char var, char* text) {
    switch(var) {
        case 0:
            strcat(text, "continous");
            break;
        case 1:
            strcat(text, "discrete");
            break;
        case 2:
            strcat(text, "scatter");
            break;
        case 3:
            strcat(text, "stem");
    }
}

// funkcija koja kontrolise promjenu parametara
void resetParameters() {
    t1.detach();
    
    int newNumber = potentSamples.read() * topSamples + botSamples;
    if(numberOfSamples != newNumber) {
        // Ukoliko se promjeni broj uzoraka, funkcija se mora ponovo crtati
        numberOfSamples = newNumber;
        if(!setup && !staticDrawing) {
            reDrawSamples();
        }
    }
    
    modeClicks = (modeClicks + mode) % numberOfModes;
    // potrebno ponovno crtanje pri promjeni tipa iscrtavanja
    if(modeClicks != mode) {
        mode = modeClicks;
        reDrawSamples();
    }
    modeClicks = 0;
    
    signalClicks = (signalClicks + function) % numberOfFunctions;
    // ukoliko se promjeni signal, vrijeme postavljamo na 0
    if(signalClicks != function) {
        T = 0;
        function = signalClicks;
        BSP_LCD_SetTextColor(plotBackground);
        BSP_LCD_FillRect(35, 45, 205, 170);
    
        BSP_LCD_SetTextColor(textColor);
        BSP_LCD_DrawHLine(0, 40, BSP_LCD_GetXSize());
        BSP_LCD_DrawHLine(35, 130, BSP_LCD_GetXSize() - 40);
        BSP_LCD_DrawVLine(40, 50, BSP_LCD_GetYSize() - 80);
    }
    signalClicks = 0;
    
    amplitude = potentAmplitude.read();
    ayoffset = potentayOffset.read();
    axoffset = potentaxOffset.read();
    frequency = potentFrequency.read();
    drawSpeed = (potentSpeed.read() - 1.5) * -1;
    
    BSP_LCD_ClearStringLine(0);
    BSP_LCD_ClearStringLine(1);
    
    char temp[20] = "Signal: ";
    appendSignalName(function, temp);
    BSP_LCD_DisplayStringAtLine(0, (uint8_t *) temp);
    
    sprintf(temp, "Mode: ");
    appendModeName(mode, temp);
    BSP_LCD_DisplayStringAtLine(1, (uint8_t *) temp);
    
    BSP_LCD_SetTextColor(detailsColor);
    BSP_LCD_DisplayStringAtLine(2, (uint8_t *) "Details");
    BSP_LCD_SetTextColor(textColor);
    
    if(details) {
        details = false;
        showDetails();
    }
    else if(staticDrawing) {
        staticDraw();
    }
    else {
        t1.attach(callback(&drawSample), drawSpeed);
    }
    setup = false;
}

// sljedecih 8 funkcija su funkcije koje racunaju uzorke
// sve nedefenisane funkcije su oblika:
// amplituda * f(x * frekvencija) - offset = A * f(x * f) - c
double sinusSample(double x) {
    double A = amplitude * topAmplitude + botAmplitude;
    double f = frequency * topFrequency + botFrequency;
    double c = ayoffset * topOffset + botOffset;
    return A * sin(x * f) - c;
}

double cosineSample(double x) {
    double A = amplitude * topAmplitude + botAmplitude;
    double f = frequency * topFrequency + botFrequency;
    double c = ayoffset * topOffset + botOffset;
    return A * cos(x * f) - c;
}

double sincSample(double x) {
    double A = amplitude * topAmplitude + botAmplitude;
    if(x == 0) {
        return A;
    }
    double f = frequency * topFrequency + botFrequency;
    double c = ayoffset * topOffset + botOffset;
    return A * (sin(x * f * 2 * PI) / (x * f * 2 * PI)) - c;
}

double squareSample(double x) {
    double A = amplitude * topAmplitude + botAmplitude;
    double f = frequency * topFrequency + botFrequency;
    double c = ayoffset * topOffset + botOffset;
    if(f < eps) {
        return A * 1 - c;
    }
    while(x > PI / f) {
        x -= 2 * PI / f;
    }
    if(x < 0) {
        return A * 1 - c;
    }
    else {
        return A * 0 - c;
    }
}

double sawSample(double x) {
    double A = amplitude * topAmplitude + botAmplitude;
    double f = frequency * topFrequency + botFrequency;
    double c = ayoffset * topOffset + botOffset;
    while(x >= PI / f) {
        x -= PI / f;
    }
    return A * x - c;
}

double triangleSample(double x) {
    double A = amplitude * topAmplitude + botAmplitude;
    double f = frequency * topFrequency + botFrequency;
    double c = ayoffset * topOffset + botOffset;
    while(x > 2 * PI / f) {
        x -= 2 * PI / f;
    }
    if(x < PI / f) {
        return A * x - c;
    }
    else {
        return (A * (x - 2 * PI / f) * (-1)) - c;
    }
}

// funkcija podrazumijeva periodicko produzenje opsega [-PI / f, PI / f]
double exponentialSample(double x) {
    double A = amplitude * topAmplitude + botAmplitude;
    double f = frequency * topFrequency + botFrequency;
    double c = ayoffset * topOffset + botOffset;
    while(x > PI / f) {
        x -= 2 * PI / f;
    }
    if(x < 0) {
        return A * pow(2, x) - c;
    }
    else {
        return A * pow(2, -x) - c;
    }
}

// Vlastita funkcija bi trebala biti periodicna
// Ne bi trebala prelaziti vrijednost vece od 9.9 niti manje od 9.9
double customFunction(double x) {
    double A = amplitude * topAmplitude + botAmplitude;
    double f = frequency * topFrequency + botFrequency;
    double c = ayoffset * topOffset + botOffset;
    return -A * sin(x * PI / f) + c;
}

// funkcija koja kontrolise racunanje uzoraka
double functionSample(double T) {
    switch(function) {
        case 0:
            return sinusSample(T);
            break;
        case 1:
            return cosineSample(T);
            break;
        case 2:
            return sincSample(T);
            break;
        case 3:
            return squareSample(T);
            break;
        case 4:
            return sawSample(T);
            break;
        case 5:
            return triangleSample(T);
            break;
        case 6:
            return exponentialSample(T);
            break;
        case 7:
            return customFunction(T);
    }
}

// funkcija koja ponovno iscrtava uzorke
void reDrawSamples() {
    // zaustavljanje crtanja i ponovno ucitavanje komponenti displeja
    t1.detach();
    BSP_LCD_SetTextColor(plotBackground);
    BSP_LCD_FillRect(35, 45, 205, 170);
    
    BSP_LCD_SetTextColor(textColor);
    BSP_LCD_DrawHLine(0, 40, BSP_LCD_GetXSize());
    BSP_LCD_DrawHLine(35, 130, BSP_LCD_GetXSize() - 40);
    BSP_LCD_DrawVLine(40, 50, BSP_LCD_GetYSize() - 80);
    {
        int i = 1;
        // potrebno za ispravno pozicioniranje tacki naspram x-ose
        int pixelAdjustment = 0;
        // Ukoliko je T < max broja uzoraka, iscrtavamo uzorke
        // od T - broj uzoraka do broj uzoraka jer zelimo iscrtati
        // zadnje izracunate uzorke (sa kraja vektora a ne pocetka)
        if(T < maxSamples - 1) {
            i = T - numberOfSamples + 1;
            if(i < 0) {
                i = 1;
            }
            pixelAdjustment = i - 1;
        }
        // u suprotnom iscrtavamo od max broj - trenutni broj do max broj
        else {
            i = maxSamples - numberOfSamples + 1;
            pixelAdjustment = i - 1;
        }
        // potrebno za promjenu opsega y-ose
        double maxEl = 1.0;
        for(; i < T && i < maxSamples; i++) {
            if(fabs(samples[i]) > maxEl) {
                maxEl = fabs(samples[i]);
            }
        }
        if(fabs((maxEl - 1) / 1.5 - maxValue) > eps) {
            maxValue = (maxEl - 1) / 1.5;
            changeLimit();
        }
        i = pixelAdjustment + 1;
        for(; i < T && i < maxSamples; i++) {
            BSP_LCD_SetTextColor(textColor);
            double x0 = 40 + int((((i - 1) - pixelAdjustment) * 195 / numberOfSamples));
            double y0 = 130 - samples[i - 1] / axValues[0] * 80;
            double x1 = 40 + int(((i - pixelAdjustment) * 195 / numberOfSamples));
            double y1 = 130 - samples[i] / axValues[0] * 80;
            drawPoint(x0, y0, x1, y1);
            // potrebno zbog sprecavanja prekoracenja
            // crtanje kontinualno ili diskretno zahtijeva dvije tacke, a ako se nalazimo
            // na zadnjem elementu vektora, tada bi crtali zadnji i zadnji + 1
            if((i == T - 1 || i == maxSamples - 1) && (mode == 2 || mode == 3)) {
                drawPoint(x1, y1, 0, -1);
            }
        }
    }
    // nastavljanje crtanja
    t1.attach(callback(&drawSample), drawSpeed);
}

// funkcija koja vrsi prelaz sa statickog na live crtanje
void dynamicDraw() {
    buttonStatic.rise(NULL);

    staticDrawing = false;
        
    BSP_LCD_SetTextColor(plotBackground);
    BSP_LCD_FillRect(35, 45, 205, 170);
    
    BSP_LCD_SetTextColor(textColor);
    BSP_LCD_DrawHLine(0, 40, BSP_LCD_GetXSize());
    BSP_LCD_DrawHLine(35, 130, BSP_LCD_GetXSize() - 40);
    BSP_LCD_DrawVLine(40, 50, BSP_LCD_GetYSize() - 80);
        
    T = 0;
    t1.attach(callback(&drawSample), drawSpeed);
    buttonStatic.fall(callback(&staticDraw));
}

// funkcija koja prelazi sa live na staticko crtanje
void staticDraw() {
    t1.detach();
    buttonStatic.rise(NULL);
    staticDrawing = true;
        
    BSP_LCD_SetTextColor(plotBackground);
    BSP_LCD_FillRect(35, 45, 205, 170);
    
    BSP_LCD_SetTextColor(textColor);
    BSP_LCD_DrawHLine(0, 40, BSP_LCD_GetXSize());
    BSP_LCD_DrawHLine(35, 130, BSP_LCD_GetXSize() - 40);
    BSP_LCD_DrawVLine(40, 50, BSP_LCD_GetYSize() - 80);
    
    // iscrtavanje
    double maxEl = 1.0;
    for(int i(0); i < numberOfSamples; i++) {
        samples[i] = functionSample(double(i) / (numberOfSamples - 1) * PI - (axoffset * topOffset - botOffset) * PI);
        if(fabs(samples[i]) > maxEl) {
            maxEl = fabs(samples[i]);
        }
    }
    if(fabs((maxEl - 1) / 1.5 - maxValue) > eps) {
            maxValue = (maxEl - 1) / 1.5;
            changeLimit();
    }
    for(int i(1); i < numberOfSamples; i++) {
        double x0 = 40 + int((i - 1) * 195 / numberOfSamples);
        double y0 = 130 - samples[i - 1] / axValues[0] * 80;
        double x1 = 40 + int(i * 195 / numberOfSamples);
        double y1 = 130 - samples[i] / axValues[0] * 80;
        drawPoint(x0, y0, x1, y1);
    }
    buttonStatic.fall(callback(&dynamicDraw));
}

// funkcija koja kontrolise crtanje signala, glavna funkcija programa
void drawSample() {
    // potrebno za promjenu opsega y-ose
    double maxEl = 1.0;
    // uslov koji sadrzi petlju koja scroll-a graf, dakle scroll-anje se vrsi
    // ukoliko je trenutno vrijeme vece od zadatog broja uzoraka
    if(T > numberOfSamples - 1) {
        // uzroke koje crtamo nalaze se u opsegu [index1, index2)
        int index1 = 0;
        int index2 = 0;
        if(T < maxSamples) {
            index1 = T - numberOfSamples;
            index2 = T;
        }
        else {
            index1 = maxSamples - numberOfSamples;
            index2 = maxSamples;
        }
        for(int i(0); i < maxSamples - 1 && i <= T; i++) {
            
            // proracun potrebnih tacki za brisanje/crtanje (pozicije piksela na displeju)
            double x1 = 40 + int(((i - index1) * 195 / numberOfSamples));
            double x2 = 40 + int(((i + 1 - index1) * 195 / numberOfSamples));
            double oldy0 = 130 - samples[i - 1] / oldaxValues[0] * 80;
            double oldy1 = 130 - samples[i] / oldaxValues[0] * 80;
            double oldy2 = 130 - samples[i + 1] / oldaxValues[0] * 80;
            double y0 = 130 - samples[i - 1] / axValues[0] * 80;
            double y1 = 130 - samples[i] / axValues[0] * 80;
            double y2 = 130 - samples[i + 1] / axValues[0] * 80;
            
            // pronalazak max elementa u opsegu
            if(fabs(samples[i]) > maxEl && (i >= index1 && i <= index2 && i < maxSamples)) {
                maxEl = fabs(samples[i]);
            }
            ////////////////////////////////////////////////////////////////////
            // brisanje zoraka
            //
            // ako se promjenio opseg, brisanje je potrebno vrisiti na osnovu
            // starih vrijednosti (parametar 'oldaxValues')
            if(refresh && (i >= index1 && i < index2 - 1)) {
                BSP_LCD_SetTextColor(plotBackground);
                if(T <= maxSamples && i >= 0) {
                    drawPoint(x1, oldy0, x2, oldy1);
                }
                else {
                    oldy2 = -1;
                    if(i < index2 - 2) {
                        oldy2 = 130 - samples[i + 1] / oldaxValues[0] * 80; 
                    }
                    drawPoint(x1, oldy1, x2, oldy2);
                }
            }
            
            // u suprotnom se tacke brisu na osnovu trenutnih vrijednosti
            else if(i >= index1 && i < index2 - 1){
                BSP_LCD_SetTextColor(plotBackground);
                if(T <= maxSamples && i >= 0) {
                    drawPoint(x1, y0, x2, y1);
                }
                else {
                    drawPoint(x1, y1, x2, y2);
                }
            }
            ////////////////////////////////////////////////////////////////////
            
            // Ukoliko je T preslo velicinu vektora uzoraka, pocinjemo shiftanje uzoraka
            if(T > maxSamples) {
                samples[i] = samples[i + 1];
            }
            
            // crtanje uzoraka
            if(i >= index1 && i < index2) {
                x1 = 40 + int(((i - index1) * 195 / numberOfSamples));
                x2 = 40 + int(((i + 1 - index1) * 195 / numberOfSamples));
                y1 = 130 - samples[i] / axValues[0] * 80;
                y2 = -1;
                if(T <= maxSamples) {
                    if(i < index2 - 2) {
                        y2 = 130 - samples[i + 1] / axValues[0] * 80;
                    }
                }
                else if(i < index2 - 2){
                    y2 = 130 - samples[i + 2] / axValues[0] * 80;
                }
                BSP_LCD_SetTextColor(textColor);
                drawPoint(x1, y1, x2, y2);
            }
        }
        if(refresh) {
            refresh = false;
        }
        BSP_LCD_SetTextColor(plotBackground);
        // Kako petlja ne uzima u obzir zadnji uzorak jer se kasnije racuna
        // potrebno je obrisati stari
        double x1 = 40 + int(((numberOfSamples - 1) * 195 / numberOfSamples));
        double y1 = 130 - samples[index2 - 1] / axValues[0] * 80;
        drawPoint(x1, y1, 0, -1);
        
        // obnavljanje osa
        BSP_LCD_SetTextColor(textColor);
        BSP_LCD_DrawHLine(35, 130, BSP_LCD_GetXSize() - 40);
        BSP_LCD_DrawVLine(40, 50, BSP_LCD_GetYSize() - 80);
    }
    /////////////////////////////////////////////////////////////////////////////
    // ukoliko je T manje od trenutnog broja uzoraka, nije potrebno scroll-ati graf
    if(T < numberOfSamples) {
        // pretraga najveceg elementa za promjenu opsega y-ose
        for(int i(0); i < T; i++) {
            if(fabs(samples[i]) > maxEl) {
                maxEl = fabs(samples[i]);
            }
        }
        samples[T] = functionSample(double(T) / maxSamples * PI);
        if(fabs(samples[T]) > maxEl) {
            maxEl = fabs(samples[T]);
        }
        if(fabs((maxEl - 1) / 1.5 - maxValue) > eps) {
            maxValue = (maxEl - 1) / 1.5;
            changeLimit();
            refresh = true;
        }
        // brisanje i ponovno iscrtavanje
        // ovaj korak je jedino potreban pri promjeni opsega y-ose jer se 
        // graf jos ne scroll-a
        if(refresh) {
            for(int i(0); i < T; i++) {
                double x1 = 40 + int((i * 195 / numberOfSamples));
                double x2 = 40 + int(((i + 1) * 195 / numberOfSamples));
                double oldy1 = 130 - samples[i] / oldaxValues[0] * 80;
                double oldy2 = -1;
                if(i < T - 1) {
                    oldy2 = 130 - samples[i + 1] / oldaxValues[0] * 80;
                }
                double y1 = 130 - samples[i] / axValues[0] * 80;
                double y2 = 130 - samples[i + 1] / axValues[0] * 80;
                BSP_LCD_SetTextColor(plotBackground);
                drawPoint(x1, oldy1, x2, oldy2);
                
                BSP_LCD_SetTextColor(textColor);
                drawPoint(x1, y1, x2, y2);
            }
            refresh = false;
        }
        double x1 = 40 + int((T * 195 / numberOfSamples));
        double y1 = 130 - samples[T] / axValues[0] * 80;
        double x2 = 40 + int(((T - 1) * 195 / numberOfSamples));;
        double y2 = -1;
        if(T > 0) {
            y2 = 130 - samples[T - 1] / axValues[0] * 80;
        }
        if(!refresh) {
            if(mode == 1 && T > 0) {
                drawPoint(x2, y2, x1, y1);
            }
            else {
                drawPoint(x1, y1, x2, y2);
            }
        }
    }
    ///////////////////////////////////////////////////////////////////////////////
    
    // ukoliko je T > trenutnog broja uzoraka, kontrola grafa se vrsi u petlji iznad
    else {
        int index = 0;
        if(T < maxSamples) {
            index = T;
        }
        else {
            index = maxSamples - 1;
        }
        samples[index] = functionSample(double(T) / maxSamples * PI);
        if(fabs(samples[index]) > maxEl) {
            maxEl = fabs(samples[index]);
        }
        if(fabs((maxEl - 1) / 1.5 - maxValue) > eps){
            maxValue = (maxEl - 1) / 1.5;
            changeLimit();
            refresh = true;
        }
        double x1 = 40 + int(((numberOfSamples - 1) * 195 / numberOfSamples));
        double y1 = 130 - samples[index] / axValues[0] * 80;
        drawPoint(x1, y1, 0, -1);
    }
    T++;
}

int main() {
    // koordinate potrebne za iscrtavanje mjeseca
    moon[0].X = 225; moon[0].Y = 222;
    moon[1].X = 230; moon[1].Y = 222;
    moon[2].X = 234; moon[2].Y = 226;
    moon[3].X = 234; moon[3].Y = 233;
    moon[4].X = 230; moon[4].Y = 237;
    moon[5].X = 225; moon[5].Y = 237;
    moon[6].X = 225; moon[6].Y = 234;
    moon[7].X = 227; moon[7].Y = 234;
    moon[8].X = 231; moon[8].Y = 230;
    moon[9].X = 231; moon[9].Y = 229;
    moon[10].X = 227; moon[10].Y = 225;
    moon[11].X = 225; moon[11].Y = 225;
    
    // ucitavanje displeja
    BSP_LCD_Init();
    if (BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize()) == TS_ERROR) {
        printf("BSP_TS_Init error\n");
    }
    printf("%d, %d\n", lcdW, lcdH);

    BSP_LCD_Clear(background);
    BSP_LCD_SetBackColor (background);
    BSP_LCD_SetTextColor(nightBackground);
    BSP_LCD_FillCircle(230, 230, 2);
    BSP_LCD_FillRect(220, 220, 20, 20);
    BSP_LCD_SetTextColor(yellow);
    BSP_LCD_FillPolygon(moon, 12);
    BSP_LCD_SetTextColor(textColor);
    
    changeLimit();
    resetParameters();
    BSP_LCD_SetTextColor(plotBackground);
    BSP_LCD_FillRect(35, 45, 205, 170);
    
    BSP_LCD_SetTextColor(textColor);
    BSP_LCD_DrawHLine(0, 40, BSP_LCD_GetXSize());
    BSP_LCD_DrawHLine(35, 130, BSP_LCD_GetXSize() - 40);
    BSP_LCD_DrawVLine(40, 50, BSP_LCD_GetYSize() - 80);
    
    buttonSet.fall(callback(&resetParameters));
    buttonStatic.rise(callback(&staticDraw));
    buttonMode.fall(callback(&incrementMode));
    buttonSignal.fall(callback(&incrementSignal));
    buttonStopDraw.fall(callback(&stopDraw));
    buttonTheme.fall(callback(&changeTheme));
    
    // Kako touch displeja ne generise interrupt, potrebno je kod vezan za touch
    // ubaciti u while petlju
    while (1) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x1 = TS_State.touchX[0];
            uint16_t y1 = TS_State.touchY[0];
            if(!details && x1 >= 220 && x1 <= 240 && y1 >= 220 && y1 <= 240) {
                changeTheme();
                wait_ms(1000);
            }
            else if(!details && x1 >= 0 && x1 <= 50 && y1 >= 27 && y1 <= 35) {
                showDetails();
                wait_ms(1000);
            }
            else if(details && x1 > 0 && x1 < 30 && y1 >= 110 && y1 <= 120) {
                showDetails();
                wait_ms(1000);
            }

        }
        wait_ms(50);
    }
}