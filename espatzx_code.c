#include <string.h>
#include <input.h>
#include <arch/zx.h>
#include <stdint.h>

// ============================================================
// EXTERNAL AY-UART DRIVER
// ============================================================

extern void ay_uart_init(void);
extern void ay_uart_send(uint8_t c) __z88dk_fastcall;
extern uint8_t ay_uart_ready(void);
extern uint8_t ay_uart_read(void);
static void uart_flush_rx(void);

// ============================================================
// FONT64 DATA
// ============================================================

#include "font64_data.h"

// ============================================================
// SCREEN CONFIGURATION
// ============================================================

#define SCREEN_COLS     64
#define SCREEN_PHYS     32

#define BANNER_START    0
#define MAIN_START      2
#define MAIN_LINES      17
#define MAIN_END        (MAIN_START + MAIN_LINES - 1)
#define STATUS_LINE     20
#define INPUT_START     21
#define INPUT_LINES     3
#define INPUT_END       23

// ============================================================
// GLOBAL STATE
// ============================================================
// ============================================================
// RING BUFFER (Buffer Circular para RX)
// ============================================================

#define RING_BUFFER_SIZE 256 
static uint8_t ring_buffer[RING_BUFFER_SIZE];
static uint8_t rb_head = 0; // Donde escribimos
static uint8_t rb_tail = 0; // Desde donde leemos

// Verifica si el buffer está lleno
static uint8_t rb_full(void)
{
    return ((uint8_t)(rb_head + 1) == rb_tail);
}

// Vuelca todo lo que tenga el chip UART a la RAM inmediatamente
static void uart_drain_to_buffer(void)
{
    // Leemos hasta vaciar el chip o un máximo seguro para no bloquear
    uint8_t max_loop = 32; 
    while (ay_uart_ready() && max_loop > 0) {
        // Protección de overflow: si buffer está casi lleno, dejar de leer
        if (rb_full()) break;
        
        // Escribimos en head
        ring_buffer[rb_head++] = ay_uart_read();
        max_loop--;
    }
}

// Saca un byte del buffer de RAM (si hay)
static int16_t rb_pop(void)
{
    if (rb_head == rb_tail) return -1; // Buffer vacío
    return ring_buffer[rb_tail++];
}

// Limpia el buffer completamente
static void rb_flush(void)
{
    uart_flush_rx(); // Limpia hardware
    rb_head = rb_tail = 0; // Resetea índices
}

#define LINE_BUFFER_SIZE 80
static char line_buffer[LINE_BUFFER_SIZE];
static uint8_t line_len = 0;

static uint8_t main_line = MAIN_START;
static uint8_t main_col = 0;

static uint8_t terminal_ready = 0;
static uint8_t debug_mode = 0;  // Use !DEBUG to enable
static uint8_t cursor_pos = 0;

// Colors
#define ATTR_BANNER   (PAPER_BLUE | INK_WHITE | BRIGHT)
#define ATTR_STATUS   (PAPER_WHITE | INK_BLUE)
#define ATTR_MAIN_BG  (PAPER_BLACK | INK_WHITE)
#define ATTR_USER     (PAPER_BLACK | INK_WHITE | BRIGHT)
#define ATTR_RESPONSE (PAPER_BLACK | INK_YELLOW | BRIGHT)
#define ATTR_LOCAL    (PAPER_BLACK | INK_GREEN | BRIGHT)
#define ATTR_DEBUG    (PAPER_BLACK | INK_CYAN)
#define ATTR_INPUT_BG (PAPER_GREEN | INK_BLACK)
#define ATTR_INPUT    (PAPER_GREEN | INK_BLACK)
#define ATTR_PROMPT   (PAPER_GREEN | INK_BLACK)

#define STATUS_RED     (PAPER_WHITE | INK_RED)
#define STATUS_GREEN   (PAPER_WHITE | INK_GREEN)
#define STATUS_YELLOW  (PAPER_WHITE | INK_YELLOW)
// NUEVO: Definimos tiempos de espera
#define TIMEOUT_STD  200000UL   // Timeout estándar para comandos AT
#define TIMEOUT_FAST 25000UL    // Timeout rápido para queries simples
#define TIMEOUT_LONG 500000UL   // Timeout largo para operaciones como SCAN o CONNECT
static uint8_t current_attr = ATTR_USER;

// Status bar data
static char device_ip[16] = "---";
static char device_ssid[20] = "---";
static int8_t device_rssi = 0;
static uint8_t connection_status = 0;
static char device_time[6] = "--:--";
static char device_at_ver[8] = "---";

// ============================================================
// COMMAND HISTORY
// ============================================================

#define HISTORY_SIZE    6
#define HISTORY_LEN     64

static char history[HISTORY_SIZE][HISTORY_LEN];
static uint8_t hist_head = 0;
static uint8_t hist_count = 0;
static int8_t hist_pos = -1;
static char temp_input[LINE_BUFFER_SIZE];

static void history_add(const char *cmd, uint8_t len)
{
    uint8_t i;
    if (len == 0) return;
    if (hist_count > 0) {
        uint8_t last = (hist_head + HISTORY_SIZE - 1) % HISTORY_SIZE;
        if (strcmp(history[last], cmd) == 0) return;
    }
    for (i = 0; i < len && i < HISTORY_LEN - 1; i++) {
        history[hist_head][i] = cmd[i];
    }
    history[hist_head][i] = 0;
    hist_head = (hist_head + 1) % HISTORY_SIZE;
    if (hist_count < HISTORY_SIZE) hist_count++;
    hist_pos = -1;
}

static void history_nav_up(void)
{
    uint8_t idx;
    if (hist_count == 0) return;
    if (hist_pos == -1) memcpy(temp_input, line_buffer, line_len + 1);
    if (hist_pos < (int8_t)(hist_count - 1)) hist_pos++;
    idx = (hist_head + HISTORY_SIZE - 1 - hist_pos) % HISTORY_SIZE;
    strcpy(line_buffer, history[idx]);
    line_len = strlen(line_buffer);
}

static void history_nav_down(void)
{
    uint8_t idx;
    if (hist_pos < 0) return;
    hist_pos--;
    if (hist_pos < 0) {
        memcpy(line_buffer, temp_input, LINE_BUFFER_SIZE);
        line_len = strlen(line_buffer);
    } else {
        idx = (hist_head + HISTORY_SIZE - 1 - hist_pos) % HISTORY_SIZE;
        strcpy(line_buffer, history[idx]);
        line_len = strlen(line_buffer);
    }
}

// Ir al comando más antiguo guardado
static void history_rewind(void)
{
    uint8_t idx;
    if (hist_count == 0) return;
    
    // Si no estábamos navegando, guardamos lo que el usuario escribió
    if (hist_pos == -1) memcpy(temp_input, line_buffer, line_len + 1);
    
    hist_pos = hist_count - 1; // Posición más antigua
    idx = (hist_head + HISTORY_SIZE - 1 - hist_pos) % HISTORY_SIZE;
    
    strcpy(line_buffer, history[idx]);
    line_len = strlen(line_buffer);
    cursor_pos = line_len; // Cursor al final
}

// Cancelar navegación y volver al input del usuario (o más reciente)
static void history_reset(void)
{
    if (hist_pos == -1) return; // Ya estamos en el input actual
    
    hist_pos = -1;
    memcpy(line_buffer, temp_input, LINE_BUFFER_SIZE);
    line_len = strlen(line_buffer);
    cursor_pos = line_len;
}

// ============================================================
// VIDEO MEMORY
// ============================================================

static uint8_t* screen_line_addr(uint8_t y, uint8_t phys_x, uint8_t scanline)
{
    uint16_t addr = 0x4000;
    addr |= ((uint16_t)(y & 0x18) << 8);
    addr |= ((uint16_t)scanline << 8);
    addr |= ((y & 0x07) << 5);
    addr |= phys_x;
    return (uint8_t*)addr;
}

static uint8_t* attr_addr(uint8_t y, uint8_t phys_x)
{
    return (uint8_t*)(0x5800 + (uint16_t)y * 32 + phys_x);
}

static void print_char64(uint8_t y, uint8_t col, uint8_t c, uint8_t attr)
{
    uint8_t phys_x = col >> 1;
    uint8_t half = col & 1;
    uint8_t *font_ptr, *screen_ptr;
    uint8_t i, font_byte, screen_byte;
    
    font_ptr = (uint8_t*)&font64[(uint16_t)c << 3];
    
    for (i = 0; i < 8; i++) {
        screen_ptr = screen_line_addr(y, phys_x, i);
        if (i == 0) font_byte = 0x00;
        else font_byte = font_ptr[i - 1];
        
        if (half == 0) {
            screen_byte = *screen_ptr & 0x0F;
            *screen_ptr = screen_byte | (font_byte & 0xF0);
        } else {
            screen_byte = *screen_ptr & 0xF0;
            *screen_ptr = screen_byte | (font_byte & 0x0F);
        }
    }
    *attr_addr(y, phys_x) = attr;
}

static void clear_line(uint8_t y, uint8_t attr)
{
    uint8_t i;
    for (i = 0; i < 8; i++) memset(screen_line_addr(y, 0, i), 0, 32);
    memset(attr_addr(y, 0), attr, 32);
}

static void clear_zone(uint8_t start, uint8_t lines, uint8_t attr)
{
    uint8_t i;
    for (i = 0; i < lines; i++) clear_line(start + i, attr);
}

static void print_str64(uint8_t y, uint8_t col, const char *s, uint8_t attr)
{
    while (*s && col < SCREEN_COLS) print_char64(y, col++, *s++, attr);
}

static void copy_screen_line(uint8_t dst_y, uint8_t src_y)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        memcpy(screen_line_addr(dst_y, 0, i), screen_line_addr(src_y, 0, i), 32);
    }
    memcpy(attr_addr(dst_y, 0), attr_addr(src_y, 0), 32);
}

static void scroll_main_zone(void)
{
    uint8_t y;
    for (y = MAIN_START; y < MAIN_END; y++) copy_screen_line(y, y + 1);
    clear_line(MAIN_END, current_attr);
}

// ============================================================
// STATUS BAR
// ============================================================

static void draw_indicator(uint8_t y, uint8_t phys_x, uint8_t attr)
{
    uint8_t *ptr;
    ptr = screen_line_addr(y, phys_x, 0); *ptr = 0x00;
    ptr = screen_line_addr(y, phys_x, 1); *ptr = 0x3C;
    ptr = screen_line_addr(y, phys_x, 2); *ptr = 0x7E;
    ptr = screen_line_addr(y, phys_x, 3); *ptr = 0x7E;
    ptr = screen_line_addr(y, phys_x, 4); *ptr = 0x7E;
    ptr = screen_line_addr(y, phys_x, 5); *ptr = 0x7E;
    ptr = screen_line_addr(y, phys_x, 6); *ptr = 0x3C;
    ptr = screen_line_addr(y, phys_x, 7); *ptr = 0x00;
    *attr_addr(y, phys_x) = attr;
}

static void draw_signal_bars(uint8_t y, uint8_t phys_x, int8_t rssi)
{
    uint8_t i, b;
    uint8_t bars = 0;
    
    // Matriz para 3 bloques de 8x8 (24px total)
    uint8_t pattern[3][8];
    memset(pattern, 0, sizeof(pattern));

    if (rssi != 0) {
        uint8_t val = (rssi < 0) ? -rssi : rssi;
        // Escala de 1 a 10
        if (val <= 40) bars = 10;
        else if (val <= 46) bars = 9;
        else if (val <= 52) bars = 8;
        else if (val <= 58) bars = 7;
        else if (val <= 64) bars = 6;
        else if (val <= 70) bars = 5;
        else if (val <= 76) bars = 4;
        else if (val <= 82) bars = 3;
        else if (val <= 88) bars = 2;
        else bars = 1;
    }

    // --- CONSTRUCCIÓN DEL PATRÓN ---
    if (bars == 0) {
        // Línea base punteada (estética "sin señal")
        pattern[0][7] = 0x55; pattern[1][7] = 0x55; pattern[2][7] = 0x55;
    } else {
        // Bloque 0 (Barras 1-4)
        if (bars >= 1) pattern[0][7] |= 0xC0;
        if (bars >= 2) pattern[0][7] |= 0x30;
        if (bars >= 3) { pattern[0][6] |= 0x0C; pattern[0][7] |= 0x0C; }
        if (bars >= 4) { pattern[0][5] |= 0x03; pattern[0][6] |= 0x03; pattern[0][7] |= 0x03; }
        // Bloque 1 (Barras 5-8)
        if (bars >= 5) { for(i=4;i<8;i++) pattern[1][i] |= 0xC0; }
        if (bars >= 6) { for(i=3;i<8;i++) pattern[1][i] |= 0x30; }
        if (bars >= 7) { for(i=2;i<8;i++) pattern[1][i] |= 0x0C; }
        if (bars >= 8) { for(i=1;i<8;i++) pattern[1][i] |= 0x03; }
        // Bloque 2 (Barras 9-10)
        if (bars >= 9)  { for(i=0;i<8;i++) pattern[2][i] |= 0xC0; }
        if (bars >= 10) { for(i=0;i<8;i++) pattern[2][i] |= 0x30; }
    }

    // COLOR: Siempre ROJO sobre BLANCO, como pediste.
    uint8_t attr = PAPER_WHITE | INK_RED;

    // Dibujar los 3 bloques
    for (b = 0; b < 3; b++) {
        for (i = 0; i < 8; i++) {
            *screen_line_addr(y, phys_x + b, i) = pattern[b][i];
        }
        *attr_addr(y, phys_x + b) = attr;
    }
}

static void int_to_str(int16_t val, char *buf)
{
    uint8_t i = 0, neg = 0;
    char tmp[8]; // Buffer ampliado
    uint8_t j = 0;
    
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    
    while (val > 0) { 
        tmp[j++] = '0' + (val % 10); 
        val /= 10; 
    }
    
    if (neg) buf[i++] = '-';
    while (j > 0) buf[i++] = tmp[--j];
    buf[i] = 0;
}

// Definiciones de colores locales para la barra
#define ATTR_LBL (PAPER_WHITE | INK_BLUE)
#define ATTR_VAL (PAPER_WHITE | INK_BLACK)

// Helper para imprimir texto y rellenar con espacios hasta un ancho fijo
// Esto evita tener que borrar la línea antes, eliminando el parpadeo
static void print_padded(uint8_t y, uint8_t col, const char *s, uint8_t attr, uint8_t width)
{
    uint8_t count = 0;
    while (*s && count < width) {
        print_char64(y, col++, *s++, attr);
        count++;
    }
    // Rellenar hueco restante con espacios
    while (count < width) {
        print_char64(y, col++, ' ', attr);
        count++;
    }
}

static void draw_status_bar(void)
{
    uint8_t ind_attr;
    char ssid_short[16];
    
    // Layout optimizado (64 cols):
    // IP: 0-17 | AT: 18-27 | SSID: 28-48 | RSSI: 48-53 | Time: 54-59 | Ind: 62-63

    // --- ZONA 1: IP (Cols 0-17) ---
    print_str64(STATUS_LINE, 0, "IP:", ATTR_LBL);
    print_padded(STATUS_LINE, 3, device_ip, ATTR_VAL, 15);
    
    // --- ZONA 2: AT Version (Cols 18-27) ---
    print_str64(STATUS_LINE, 18, "AT:", ATTR_LBL);
    print_padded(STATUS_LINE, 21, device_at_ver, ATTR_VAL, 7);
    
    // --- ZONA 3: SSID (Cols 28-47) ---
    print_str64(STATUS_LINE, 28, "SSID:", ATTR_LBL);
    // Truncar SSID a 14 chars + ~
    if (strlen(device_ssid) > 14) {
        memcpy(ssid_short, device_ssid, 13);
        ssid_short[13] = '~';
        ssid_short[14] = 0;
    } else {
        strcpy(ssid_short, device_ssid);
    }
    print_padded(STATUS_LINE, 33, ssid_short, ATTR_VAL, 15);
    
    // --- ZONA 4: RSSI Barras (Cols 48-53, Phys 24-26) ---
    draw_signal_bars(STATUS_LINE, 24, device_rssi);
    
    // --- ZONA 5: Time (Cols 54-59) ---
    print_padded(STATUS_LINE, 54, device_time, ATTR_VAL, 5);
    print_char64(STATUS_LINE, 59, ' ', ATTR_VAL);
    print_char64(STATUS_LINE, 60, ' ', ATTR_STATUS);
    print_char64(STATUS_LINE, 61, ' ', ATTR_STATUS);

    // --- ZONA 6: Status Indicator (Cols 62-63, Phys 31) ---
    if (connection_status == 0) ind_attr = STATUS_RED;
    else if (connection_status == 1) ind_attr = STATUS_GREEN;
    else ind_attr = STATUS_YELLOW;
    
    draw_indicator(STATUS_LINE, 31, ind_attr);
}

// ============================================================
// MAIN ZONE OUTPUT
// ============================================================

static void main_newline(void)
{
    main_col = 0;
    main_line++;
    if (main_line > MAIN_END) {
        scroll_main_zone();
        main_line = MAIN_END;
    }
}

static void main_putchar(uint8_t c)
{
    if (c == 13 || c == 10) { main_newline(); return; }
    if (main_col >= SCREEN_COLS) main_newline();
    print_char64(main_line, main_col++, c, current_attr);
}

static void main_puts(const char *s)
{
    while (*s) main_putchar(*s++);
}

// ============================================================
// INPUT ZONE
// ============================================================

// Dibuja una línea en la parte inferior de la celda (Cursor tipo _)
static void draw_cursor_underline(uint8_t y, uint8_t col)
{
    uint8_t phys_x = col >> 1;
    uint8_t half = col & 1;
    uint8_t *screen_ptr;
    
    // Scanline 7 es la última línea de la celda (la de abajo)
    screen_ptr = screen_line_addr(y, phys_x, 7);
    
    if (half == 0) {
        // Parte izquierda de la celda física (High nibble)
        *screen_ptr |= 0xF0; 
    } else {
        // Parte derecha de la celda física (Low nibble)
        *screen_ptr |= 0x0F;
    }
    // Forzar atributo brillante para que destaque, pero sin flash molesto
    *attr_addr(y, phys_x) = ATTR_INPUT;
}

static void redraw_input_from(uint8_t start_pos)
{
    uint8_t row, col, i;
    uint16_t abs_pos;

    // 1. Dibujar el prompt ">" si estamos repintando desde el inicio
    if (start_pos == 0) {
        print_char64(INPUT_START, 0, '>', ATTR_PROMPT);
    }

    // 2. Dibujar el texto actual (desde start_pos)
    for (i = start_pos; i < line_len; i++) {
        abs_pos = i + 2; // +2 por el prompt "> "
        row = INPUT_START + (abs_pos / SCREEN_COLS);
        col = abs_pos % SCREEN_COLS;
        if (row > INPUT_END) break;
        print_char64(row, col, line_buffer[i], ATTR_INPUT);
    }

    // 3. Dibujar el Cursor
    uint16_t cur_abs = cursor_pos + 2; 
    uint8_t cur_row = INPUT_START + (cur_abs / SCREEN_COLS);
    uint8_t cur_col = cur_abs % SCREEN_COLS;

    if (cur_row <= INPUT_END) {
        char c_under = (cursor_pos < line_len) ? line_buffer[cursor_pos] : ' ';
        print_char64(cur_row, cur_col, c_under, ATTR_INPUT);
        draw_cursor_underline(cur_row, cur_col);
    }

    // 4. LIMPIEZA LIMITADA: Solo borrar unos pocos caracteres después del texto
    // Esto es suficiente para casos de edición normal y mucho más rápido
    uint16_t end_abs = line_len + 2;
    row = INPUT_START + (end_abs / SCREEN_COLS);
    col = end_abs % SCREEN_COLS;
    
    // Borrar máximo 8 caracteres extra (suficiente para edición normal)
    uint8_t clear_count = 0;
    while (row <= INPUT_END && clear_count < 8) {
        if (!(row == cur_row && col == cur_col)) {
            print_char64(row, col, ' ', ATTR_INPUT_BG);
        }
        col++;
        if (col >= SCREEN_COLS) {
            col = 0;
            row++;
        }
        clear_count++;
    }
}

static void input_clear(void)
{
    line_len = 0; 
    line_buffer[0] = 0; 
    cursor_pos = 0; 
    hist_pos = -1;
    
    // Limpieza completa de la zona de input
    clear_zone(INPUT_START, INPUT_LINES, ATTR_INPUT_BG);
    
    // Dibujar prompt y cursor inicial
    print_char64(INPUT_START, 0, '>', ATTR_PROMPT);
    print_char64(INPUT_START, 2, ' ', ATTR_INPUT);
    draw_cursor_underline(INPUT_START, 2);
}

static void input_add_char(uint8_t c)
{
    // Lógica de mayúsculas:
    // - Comandos !: mayúsculas solo para el comando (antes del primer espacio)
    // - Comandos AT: mayúsculas SIEMPRE, excepto dentro de comillas
    // - Dentro de comillas: siempre preservar case original
    uint8_t force_upper = 1;
    uint8_t in_quotes = 0;
    uint8_t is_bang_cmd = 0;
    uint8_t found_space = 0;
    uint8_t i;
    
    // Detectar si es comando ! o AT
    if (line_len > 0 && line_buffer[0] == '!') {
        is_bang_cmd = 1;
    }
    
    // Recorrer buffer para detectar comillas y espacios
    for (i = 0; i < line_len; i++) {
        if (line_buffer[i] == '"') in_quotes = !in_quotes;
        if (line_buffer[i] == ' ' && !in_quotes) {
            found_space = 1;
        }
    }
    
    // También verificar si el carácter actual abre/cierra comillas
    if (c == '"') in_quotes = !in_quotes;
    
    // Determinar si forzar mayúsculas:
    // - Dentro de comillas: NUNCA forzar
    // - Comando !: solo forzar antes del primer espacio
    // - Comando AT u otro: forzar siempre (fuera de comillas)
    if (in_quotes) {
        force_upper = 0;
    } else if (is_bang_cmd && found_space) {
        force_upper = 0;
    }
    
    // Aplicar conversión
    if (force_upper && c >= 'a' && c <= 'z') c -= 32;

    if (c >= 32 && c < 127 && line_len < LINE_BUFFER_SIZE - 1) {
        
        // CASO 1: Inserción en medio del texto (Lento, pero necesario)
        if (cursor_pos < line_len) {
            memmove(&line_buffer[cursor_pos + 1], &line_buffer[cursor_pos], line_len - cursor_pos);
            line_buffer[cursor_pos] = c;
            line_len++;
            cursor_pos++;
            line_buffer[line_len] = 0;
            redraw_input_from(cursor_pos - 1);
        } 
        // CASO 2: Escritura normal al final (OPTIMIZADO RÁPIDO)
        else {
            line_buffer[cursor_pos] = c;
            line_len++;
            cursor_pos++;
            line_buffer[line_len] = 0;
            
            // 1. Pintamos SOLO el carácter que acabamos de escribir
            uint16_t char_abs = (cursor_pos - 1) + 2;
            uint8_t row = INPUT_START + (char_abs / SCREEN_COLS);
            uint8_t col = char_abs % SCREEN_COLS;
            print_char64(row, col, c, ATTR_INPUT);
            
            // 2. Pintamos el NUEVO cursor en la nueva posición
            uint16_t cur_abs = cursor_pos + 2;
            uint8_t cur_row = INPUT_START + (cur_abs / SCREEN_COLS);
            uint8_t cur_col = cur_abs % SCREEN_COLS;
            
            if (cur_row <= INPUT_END) {
                print_char64(cur_row, cur_col, ' ', ATTR_INPUT);
                draw_cursor_underline(cur_row, cur_col);
            }
        }
    }
}

// Función auxiliar: Repinta SOLO el carácter en la posición 'idx'
// 'show_cursor': 1 para dibujar la rayita debajo, 0 para quitarla.
static void refresh_cursor_char(uint8_t idx, uint8_t show_cursor)
{
    uint16_t abs_pos = idx + 2; // +2 por el prompt "> "
    uint8_t row = INPUT_START + (abs_pos / SCREEN_COLS);
    uint8_t col = abs_pos % SCREEN_COLS;
    
    if (row > INPUT_END) return;

    // 1. Pintar el carácter limpio (esto borra el cursor antiguo si lo había)
    // Si estamos al final de la linea, pintamos un espacio
    char c = (idx < line_len) ? line_buffer[idx] : ' ';
    print_char64(row, col, c, ATTR_INPUT);

    // 2. Dibujar cursor (rayita abajo) si corresponde
    if (show_cursor) {
        draw_cursor_underline(row, col);
    }
}

static void input_backspace(void)
{
    if (cursor_pos > 0) {
        uint8_t was_at_end = (cursor_pos == line_len);
        
        cursor_pos--;
        
        if (cursor_pos < line_len - 1) {
            // Caso: borrar en medio - hay que mover texto
            memmove(&line_buffer[cursor_pos], &line_buffer[cursor_pos + 1], line_len - cursor_pos - 1);
        }
        line_len--;
        line_buffer[line_len] = 0;
        
        if (was_at_end) {
            // CASO OPTIMIZADO: Borrar al final de línea
            // Solo necesitamos:
            // 1. Borrar el carácter que estaba en la posición anterior del cursor
            // 2. Dibujar el nuevo cursor
            
            uint16_t old_pos = cursor_pos + 1 + 2; // Posición del char borrado (+2 por prompt)
            uint8_t old_row = INPUT_START + (old_pos / SCREEN_COLS);
            uint8_t old_col = old_pos % SCREEN_COLS;
            if (old_row <= INPUT_END) {
                print_char64(old_row, old_col, ' ', ATTR_INPUT_BG);
            }
            
            // Dibujar cursor en nueva posición
            refresh_cursor_char(cursor_pos, 1);
        } else {
            // Caso: borrar en medio - redibujar desde cursor
            redraw_input_from(cursor_pos);
        }
    }
}

static void set_input_busy(uint8_t is_busy)
{
    uint16_t cur_abs;
    uint8_t row, col;
    char c_under;

    if (is_busy) {
        // --- OCULTAR CURSOR ---
        cur_abs = cursor_pos + 2; 
        row = INPUT_START + (cur_abs / SCREEN_COLS);
        col = cur_abs % SCREEN_COLS;
        
        if (row <= INPUT_END) {
            // Redibujamos el carácter que hay debajo PERO con el atributo normal (sin brillo/flash extra)
            // y SIN llamar a draw_cursor_underline
            c_under = (cursor_pos < line_len) ? line_buffer[cursor_pos] : ' ';
            print_char64(row, col, c_under, ATTR_INPUT);
        }
    } else {
        // --- MOSTRAR CURSOR ---
        // Simplemente forzamos un redibujado desde la posición actual
        redraw_input_from(cursor_pos);
    }
}

static void input_left(void) {
    if (cursor_pos > 0) {
        // 1. Borrar visualmente cursor de la posición actual
        refresh_cursor_char(cursor_pos, 0);
        
        // 2. Mover la variable
        cursor_pos--;
        
        // 3. Pintar visualmente cursor en la nueva posición
        refresh_cursor_char(cursor_pos, 1);
    }
}

static void input_right(void) {
    if (cursor_pos < line_len) {
        refresh_cursor_char(cursor_pos, 0); // Borrar
        cursor_pos++;
        refresh_cursor_char(cursor_pos, 1); // Pintar nuevo
    }
}

static void input_home(void) {
    if (cursor_pos != 0) {
        refresh_cursor_char(cursor_pos, 0);
        cursor_pos = 0;
        refresh_cursor_char(cursor_pos, 1);
    }
}

static void input_end(void) {
    if (cursor_pos != line_len) {
        refresh_cursor_char(cursor_pos, 0);
        cursor_pos = line_len;
        refresh_cursor_char(cursor_pos, 1);
    }
}

// ============================================================
// UART COMMUNICATION
// ============================================================

#define RX_LINE_SIZE 100
static char rx_line[RX_LINE_SIZE];
static uint8_t rx_pos = 0;

#define RESP_WAITING    0
#define RESP_GOT_OK     1
#define RESP_GOT_ERROR  2
#define RESP_TIMEOUT    3

static void delay(uint16_t count) { while (count--) __asm__("nop"); }

static void uart_flush_rx(void)
{
    uint16_t max_wait = 500;
    uint16_t max_bytes = 500;
    
    // Drain everything
    while (max_bytes > 0) {
        if (ay_uart_ready()) { 
            ay_uart_read(); 
            max_bytes--;
            max_wait = 100;
        } else {
            if (max_wait == 0) break;
            max_wait--;
        }
    }
}

// Extra aggressive flush with delay
static void uart_flush_hard(void)
{
    uint8_t i;
    // Espera breve para datos pendientes (reducido de 5+3 a 2+1)
    for (i = 0; i < 2; i++) {
        __asm__("ei");
        __asm__("halt");
    }
    uart_flush_rx();
    __asm__("ei");
    __asm__("halt");
    uart_flush_rx();
}

static void uart_send_string(const char *s) { while (*s) ay_uart_send(*s++); }

static uint8_t is_terminator(void)
{
    if (rx_pos == 2 && rx_line[0] == 'O' && rx_line[1] == 'K') return RESP_GOT_OK;
    if (rx_pos >= 5 && rx_line[0] == 'E' && rx_line[1] == 'R' && rx_line[2] == 'R') return RESP_GOT_ERROR;
    if (rx_pos >= 4 && rx_line[0] == 'F' && rx_line[1] == 'A' && rx_line[2] == 'I') return RESP_GOT_ERROR;
    return 0;
}

// Detecta mensajes asíncronos del ESP que debemos IGNORAR
static uint8_t is_async_noise(void)
{
    if (rx_pos < 2) return 0;
    
    // +IPD (datos entrantes de conexión)
    if (rx_line[0] == '+' && rx_pos >= 4 && 
        rx_line[1] == 'I' && rx_line[2] == 'P' && rx_line[3] == 'D') return 1;
    
    // Mensajes de conexión TCP/IP
    if (rx_pos >= 4 && rx_line[0] == 'C' && rx_line[1] == 'O' && 
        rx_line[2] == 'N' && rx_line[3] == 'N') return 1;  // CONNECT, CONNECTED
    if (rx_pos >= 4 && rx_line[0] == 'C' && rx_line[1] == 'L' && 
        rx_line[2] == 'O' && rx_line[3] == 'S') return 1;  // CLOSED
    
    // Mensajes WiFi asíncronos
    if (rx_pos >= 4 && rx_line[0] == 'W' && rx_line[1] == 'I' && 
        rx_line[2] == 'F' && rx_line[3] == 'I') return 1;  // WIFI CONNECTED, WIFI GOT IP
    
    // Mensajes de servidor propietarios
    if (rx_pos >= 4 && rx_line[0] == 'L' && rx_line[1] == 'A' && 
        rx_line[2] == 'I' && rx_line[3] == 'N') return 1;
    if (rx_pos >= 4 && rx_line[0] == 'W' && rx_line[1] == 'F' && 
        rx_line[2] == 'X' && rx_line[3] == 'R') return 1;
    
    // Números solos (IDs de conexión o basura) - 1 o 2 dígitos solos
    if (rx_pos <= 2 && rx_line[0] >= '0' && rx_line[0] <= '9') {
        if (rx_pos == 1) return 1;
        if (rx_pos == 2 && rx_line[1] >= '0' && rx_line[1] <= '9') return 1;
    }
    
    // "busy" messages
    if (rx_pos >= 4 && rx_line[0] == 'b' && rx_line[1] == 'u' && 
        rx_line[2] == 's' && rx_line[3] == 'y') return 1;
    
    // "ready" del ESP (tras reset)
    if (rx_pos >= 5 && rx_line[0] == 'r' && rx_line[1] == 'e' && 
        rx_line[2] == 'a' && rx_line[3] == 'd' && rx_line[4] == 'y') return 1;
    
    // "SEND OK" / "SEND FAIL" - pero solo si no esperamos respuesta de envío
    // Lo dejamos pasar porque puede ser útil
    
    return 0;
}

static uint8_t is_valid_response(void)
{
    if (rx_pos == 0) return 0;
    
    // PRIMERO: Filtrar ruido conocido
    if (is_async_noise()) return 0;
    
    // Respuestas AT+ (comandos con respuesta estructurada)
    if (rx_line[0] == '+') return 1;
    
    // Strings entrecomillados (datos)
    if (rx_line[0] == '"') return 1;
    
    // Echo de comandos AT (si echo está habilitado)
    if (rx_pos >= 2 && rx_line[0] == 'A' && rx_line[1] == 'T') return 1;
    
    // Versión SDK
    if (rx_pos >= 3 && rx_line[0] == 'S' && rx_line[1] == 'D' && rx_line[2] == 'K') return 1;
    
    // Versión de compilación
    if (rx_pos >= 4 && rx_line[0] == 'c' && rx_line[1] == 'o' && 
        rx_line[2] == 'm' && rx_line[3] == 'p') return 1;
    
    // Direcciones IP (formato x.x.x.x con al menos 3 puntos)
    if (rx_line[0] >= '0' && rx_line[0] <= '9') {
        uint8_t i, dots = 0;
        for (i = 0; i < rx_pos; i++) if (rx_line[i] == '.') dots++;
        if (dots >= 3) return 1;
    }
    
    // MAC addresses (contienen varios ':')
    if (rx_pos >= 17) {
        uint8_t i, colons = 0;
        for (i = 0; i < rx_pos; i++) if (rx_line[i] == ':') colons++;
        if (colons >= 5) return 1;
    }
    
    // Respuestas de scan WiFi (empiezan con "(")
    if (rx_line[0] == '(') return 1;
    
    // SEND OK, SEND FAIL
    if (rx_pos >= 4 && rx_line[0] == 'S' && rx_line[1] == 'E' && 
        rx_line[2] == 'N' && rx_line[3] == 'D') return 1;
    
    // Recv X bytes
    if (rx_pos >= 4 && rx_line[0] == 'R' && rx_line[1] == 'e' && 
        rx_line[2] == 'c' && rx_line[3] == 'v') return 1;
    
    // "no change" response
    if (rx_pos >= 6 && rx_line[0] == 'n' && rx_line[1] == 'o' && rx_line[2] == ' ') return 1;
    
    return 0;
}

static void show_rx_line(void)
{
    uint8_t i;
    current_attr = ATTR_RESPONSE;
    for (i = 0; i < rx_pos; i++) main_putchar(rx_line[i]);
    main_newline();
}

static void debug_show_line(const char *prefix)
{
    uint8_t i;
    if (!debug_mode) return;
    current_attr = ATTR_DEBUG;
    main_puts(prefix);
    for (i = 0; i < rx_pos && i < 50; i++) main_putchar(rx_line[i]);
    main_newline();
}

static uint8_t try_read_line(void)
{
    int16_t val;
    uint8_t c;
    
    // 1. PRIMERO: Drenar hardware a RAM
    // Esto es lo más importante. Aseguramos los datos antes de procesar.
    uart_drain_to_buffer();

    // 2. LUEGO: Procesar desde RAM
    // Intentamos montar la línea con lo que hay en el buffer
    while ((val = rb_pop()) != -1) {
        c = (uint8_t)val;
        
        if (c == 13 || c == 10) {
            // Si encontramos salto de línea y tenemos texto
            if (rx_pos > 0) { 
                rx_line[rx_pos] = 0; 
                return 1; // ¡Línea completa encontrada!
            }
            continue; // Ignorar líneas vacías iniciales
        }
        
        if (c >= 32 && c < 127 && rx_pos < RX_LINE_SIZE - 1) {
            rx_line[rx_pos++] = c;
        }
    }
    
    return 0; // Aún no hay línea completa
}

static uint8_t wait_at_response(void)
{
    uint32_t timeout = 0;     // Cambiado a 32 bits
    uint32_t silence = 0;     // Cambiado a 32 bits
    uint8_t terminator;
    
    rx_pos = 0;
    while (timeout < TIMEOUT_STD) { // Usamos la nueva constante grande
        if (try_read_line()) {
            silence = 0;
            terminator = is_terminator();
            if (terminator) { show_rx_line(); return terminator; }
            if (is_valid_response()) show_rx_line();
            rx_pos = 0;
        } else {
            silence++;
            // Aumentamos el umbral de silencio para no cortar respuestas lentas
            if (silence > 200000UL) break; 
        }
        
        // Pequeño delay artificial para no saturar 100% CPU en bucles vacíos
        // y dar tiempo al hardware UART a recibir bits
        if ((timeout % 16) == 0) {
             uart_drain_to_buffer(); // Drenamos activamente mientras esperamos
        }
        
        timeout++;
    }
    return RESP_TIMEOUT;
}

// ============================================================
// SMART INIT - Reuses existing connection
// ============================================================

static uint8_t probe_esp(void)
{
    uint32_t timeout;
    uint8_t tries;
    uint8_t got_anything = 0;
    uint32_t try_timeout;
    
    for (tries = 0; tries < 3; tries++) {
        uart_flush_hard();
        uart_send_string("AT\r\n");
        rx_pos = 0;
        timeout = 0;
        got_anything = 0;
        
        // Primer intento más corto (ESP ya listo responde en <5ms)
        // Siguientes intentos más largos por si está ocupado
        try_timeout = (tries == 0) ? 8000UL : TIMEOUT_FAST;
        
        while (timeout < try_timeout) {
            if (try_read_line()) {
                got_anything = 1;
                
                if (debug_mode) {
                    current_attr = ATTR_DEBUG;
                    main_puts("[");
                    main_puts(rx_line);
                    main_puts("]");
                    main_newline();
                }
                
                if (rx_pos >= 2 && rx_line[0] == 'O' && rx_line[1] == 'K') return 1;
                rx_pos = 0;
            }
            timeout++;
        }
        
        // Show retry indicator only in debug
        if (debug_mode) {
            current_attr = ATTR_LOCAL;
            if (got_anything) main_puts("(no OK)");
            else main_puts("(silent)");
        }
    }
    
    return 0;
}

static uint8_t check_has_ip(void)
{
    uint32_t timeout;
    uint8_t i, j;
    char buf[16];
    uint8_t found = 0;
    
    uart_flush_hard();
    uart_send_string("AT+CIFSR\r\n");
    rx_pos = 0;
    timeout = 0;
    
    while (timeout < TIMEOUT_FAST && !found) {  
        if (try_read_line()) {
            // Debug output
            if (debug_mode) {
                current_attr = ATTR_DEBUG;
                main_puts("CIFSR:");
                main_puts(rx_line);
                main_newline();
            }
            
            // Skip async noise
            if (rx_line[0] >= '0' && rx_line[0] <= '9') { rx_pos = 0; continue; }
            if (rx_pos >= 4 && rx_line[0] == '+' && rx_line[1] == 'I' && 
                rx_line[2] == 'P' && rx_line[3] == 'D') { rx_pos = 0; continue; }
            if (rx_pos >= 4 && rx_line[0] == 'C' && rx_line[1] == 'O' && 
                rx_line[2] == 'N' && rx_line[3] == 'N') { rx_pos = 0; continue; }
            if (rx_pos >= 4 && rx_line[0] == 'C' && rx_line[1] == 'L' && 
                rx_line[2] == 'O' && rx_line[3] == 'S') { rx_pos = 0; continue; }
            if (rx_pos >= 4 && rx_line[0] == 'L' && rx_line[1] == 'A' && 
                rx_line[2] == 'I' && rx_line[3] == 'N') { rx_pos = 0; continue; }
            if (rx_pos >= 4 && rx_line[0] == 'W' && rx_line[1] == 'F' && 
                rx_line[2] == 'X' && rx_line[3] == 'R') { rx_pos = 0; continue; }
            
            // Look for +CIFSR:STAIP,"x.x.x.x"
            if (rx_pos > 10 && rx_line[0] == '+' && rx_line[1] == 'C' &&
                rx_line[2] == 'I' && rx_line[3] == 'F' && rx_line[4] == 'S' &&
                rx_line[5] == 'R' && rx_line[6] == ':') {
                
                for (i = 7; i < rx_pos - 4; i++) {
                    if (rx_line[i] == 'S' && rx_line[i+1] == 'T' && 
                        rx_line[i+2] == 'A' && rx_line[i+3] == 'I') {
                        while (i < rx_pos && rx_line[i] != '"') i++;
                        i++;
                        j = 0;
                        while (i < rx_pos && rx_line[i] != '"' && j < 15) {
                            buf[j++] = rx_line[i++];
                        }
                        buf[j] = 0;
                        
                        if (j >= 7 && buf[0] != '0') {
                            strcpy(device_ip, buf);
                            found = 1;
                        }
                        break;
                    }
                }
            }
            
            if (rx_pos == 2 && rx_line[0] == 'O' && rx_line[1] == 'K') break;
            rx_pos = 0;
        }
        timeout++;
    }
    
    uart_flush_rx();
    return found;
}

static void get_ssid_rssi(void)
{
    uint32_t timeout;
    uint8_t i, j;
    char buf[32];
    uint8_t found_line = 0;
    
    uart_flush_hard();
    uart_send_string("AT+CWJAP?\r\n");
    
    rx_pos = 0;
    timeout = 0;
    
    while (timeout < TIMEOUT_FAST) {
        if (try_read_line()) {
            
            if (rx_pos < 5) { rx_pos = 0; continue; }
            
            // Debug: ver qué llega
            if (debug_mode) {
                current_attr = ATTR_DEBUG;
                main_puts("RX:"); main_puts(rx_line); main_newline();
            }

            // Buscar "+CWJAP:"
            for (i = 0; i < rx_pos - 6; i++) {
                if (rx_line[i] == '+' && rx_line[i+1] == 'C' && rx_line[i+2] == 'W' && rx_line[i+3] == 'J') {
                    
                    found_line = 1;
                    
                    // --- 1. LEER SSID ---
                    while (i < rx_pos && rx_line[i] != '"') i++;
                    i++; // saltar comilla inicio
                    
                    j = 0;
                    while (i < rx_pos && rx_line[i] != '"' && j < 19) {
                        buf[j++] = rx_line[i++];
                    }
                    buf[j] = 0;
                    if (j > 0) strcpy(device_ssid, buf);
                    
                    // --- 2. LEER RSSI (Buscando la 3ª coma) ---
                    // Formato: +CWJAP:"ssid","bssid",channel,rssi
                    
                    uint8_t commas = 0;
                    while (i < rx_pos) {
                        if (rx_line[i] == ',') {
                            commas++;
                            // Queremos lo que hay DESPUÉS de la 3ª coma
                            // (Nota: como 'i' ya pasó el SSID, esta será la 1ª coma que veamos en el bucle,
                            // que corresponde a la coma tras el SSID. Necesitamos contar hasta llegar al RSSI).
                            
                            // Comas esperadas desde el inicio de la línea:
                            // 1ª: tras SSID
                            // 2ª: tras BSSID
                            // 3ª: tras Channel -> AQUI viene el RSSI
                            
                            // Mi bucle empieza DESPUÉS del SSID. Así que veré:
                            // Coma 1 (la que separa SSID de BSSID) -> commas=1
                            // ... BSSID ...
                            // Coma 2 (la que separa BSSID de Channel) -> commas=2
                            // ... Channel ...
                            // Coma 3 (la que separa Channel de RSSI) -> commas=3 -> LEER AHORA!
                            
                            if (commas == 3) {
                                i++; // saltar la coma
                                
                                // Leer número (con signo)
                                int16_t val = 0;
                                int8_t sign = 1;
                                
                                // Saltar espacios si los hay
                                while(i < rx_pos && rx_line[i] == ' ') i++;
                                
                                if (i < rx_pos && rx_line[i] == '-') {
                                    sign = -1;
                                    i++;
                                }
                                
                                while (i < rx_pos && rx_line[i] >= '0' && rx_line[i] <= '9') {
                                    val = val * 10 + (rx_line[i] - '0');
                                    i++;
                                }
                                
                                if (val != 0) {
                                    device_rssi = (int8_t)(val * sign);
                                }
                                break; // Fin del while de comas
                            }
                        }
                        i++;
                    }
                    break; // Fin del for principal
                }
            }
            
            if (found_line) break;
            if (rx_pos >= 2 && rx_line[0] == 'O' && rx_line[1] == 'K') break;
            if (rx_pos >= 4 && rx_line[0] == 'F' && rx_line[1] == 'A') break;
            rx_pos = 0;
        }
        timeout++;
    }
    
    if (found_line) uart_flush_rx();
}

static void get_at_version(void)
{
    uint32_t timeout;
    uint8_t i, j;
    uint8_t found = 0;
    
    uart_flush_hard();
    uart_send_string("AT+GMR\r\n");
    
    rx_pos = 0;
    timeout = 0;
    
    while (timeout < TIMEOUT_FAST) {
        if (try_read_line()) {
            
            // Debug
            if (debug_mode) {
                current_attr = ATTR_DEBUG;
                main_puts("GMR:"); main_puts(rx_line); main_newline();
            }
            
            // Buscar "AT version:" en la línea
            // Formato: "AT version:1.7.4.0(May..."
            if (!found && rx_pos > 12) {
                for (i = 0; i < rx_pos - 10; i++) {
                    if (rx_line[i] == 'A' && rx_line[i+1] == 'T' && 
                        rx_line[i+2] == ' ' && rx_line[i+3] == 'v') {
                        // Encontrado "AT v", buscar ':'
                        while (i < rx_pos && rx_line[i] != ':') i++;
                        i++; // saltar ':'
                        
                        // Extraer versión (solo números y puntos hasta '(' o espacio)
                        j = 0;
                        while (i < rx_pos && j < 6) {
                            char c = rx_line[i];
                            if (c >= '0' && c <= '9') {
                                device_at_ver[j++] = c;
                            } else if (c == '.' && j > 0) {
                                // Solo añadir punto si hay dígito antes y después
                                if (i + 1 < rx_pos && rx_line[i+1] >= '0' && rx_line[i+1] <= '9') {
                                    device_at_ver[j++] = c;
                                } else {
                                    break; // Punto final, parar
                                }
                            } else {
                                break;
                            }
                            i++;
                        }
                        device_at_ver[j] = 0;
                        found = 1;
                        break;
                    }
                }
            }
            
            // Esperar OK antes de salir
            if (rx_pos >= 2 && rx_line[0] == 'O' && rx_line[1] == 'K') break;
            rx_pos = 0;
        }
        timeout++;
    }
    
    uart_flush_hard(); // Limpieza agresiva al salir
}

static void uart_init(void)
{
    uint8_t i;
    
    ay_uart_init();
    
    // Espera mínima para que el UART esté listo (reducido de 30 a 10)
    for (i = 0; i < 10; i++) {
        __asm__("ei");
        __asm__("halt");
    }
    
    uart_flush_rx();
    uart_send_string("ATE0\r\n");
    
    // Espera respuesta ATE0 (reducido de 15 a 5)
    for (i = 0; i < 5; i++) {
        __asm__("ei");
        __asm__("halt");
    }
    
    uart_flush_rx();
    
    // Disable multi-connection mode to reduce server noise
    uart_send_string("AT+CIPMUX=0\r\n");
    
    // Espera respuesta CIPMUX (reducido de 15 a 5)
    for (i = 0; i < 5; i++) {
        __asm__("ei");
        __asm__("halt");
    }
    
    uart_flush_rx();
}

static void smart_init(void)
{
    uint8_t i;
    
    current_attr = ATTR_LOCAL;
    main_puts("Initializing...");
    main_newline();
    
    uart_init();
    
    main_puts("Probing ESP...");
    
    if (!probe_esp()) {
        main_newline();
        main_puts("Retrying...");
        main_newline();
        
        // Reducido de 50 a 20 halts (1s -> 400ms)
        for (i = 0; i < 20; i++) {
            __asm__("ei");
            __asm__("halt");
        }
        uart_flush_rx();
        
        if (!probe_esp()) {
            main_newline();
            main_puts("ESP not responding!");
            main_newline();
            connection_status = 0;
            draw_status_bar();
            return;
        }
    }
    
    main_puts(" OK");
    main_newline();
    
    // Obtener versión AT y verificar conexión en paralelo conceptual
    // (se ejecutan secuencialmente pero sin delays extras)
    get_at_version();
    
    main_puts("Checking connection...");
    main_newline();
    
    if (check_has_ip()) {
        main_puts("Connected: ");
        main_puts(device_ip);
        main_newline();
        get_ssid_rssi();
        connection_status = 1;
    } else {
        main_puts("No WiFi connection");
        main_newline();
        connection_status = 0;
    }
    draw_status_bar();
}

static void check_connection(void)
{
    connection_status = 2;
    draw_status_bar();
    strcpy(device_ip, "---");
    strcpy(device_ssid, "---");
    device_rssi = 0;
    
    if (check_has_ip()) {
        get_ssid_rssi();
        connection_status = 1;
    } else {
        connection_status = 0;
    }
    draw_status_bar();
}

// ============================================================
// SEND AT COMMAND
// ============================================================

// Versión simplificada: solo envía los datos, no gestiona la UI
static void execute_raw_at_command(const char *cmd)
{
    uint8_t i, result;
    uint8_t len = strlen(cmd);
    
    uart_flush_rx();
    for (i = 0; i < len; i++) ay_uart_send(cmd[i]);
    ay_uart_send(13);
    ay_uart_send(10);
    
    result = wait_at_response();
    
    if (result == RESP_TIMEOUT) {
        current_attr = ATTR_LOCAL;
        main_puts("[Timeout]");
        main_newline();
    }
    uart_flush_rx();
}

// ============================================================
// LOCAL COMMANDS
// ============================================================

static uint8_t cmd_match(const char *cmd)
{
    uint8_t i = 0;
    while (cmd[i]) {
        char c1 = line_buffer[i], c2 = cmd[i];
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return 0;
        i++;
    }
    return (line_buffer[i] == 0 || line_buffer[i] == ' ');
}

static void cmd_cls(void) { clear_zone(MAIN_START, MAIN_LINES, ATTR_MAIN_BG); main_line = MAIN_START; main_col = 0; }

static void cmd_ip(void) { current_attr = ATTR_LOCAL; main_puts("Refreshing..."); main_newline(); check_connection(); }

static void cmd_scan(void) 
{ 
    current_attr = ATTR_LOCAL; 
    main_puts("Scanning..."); 
    main_newline(); 
    
    rb_flush(); // Usamos nuestra nueva función de limpieza total
    uart_send_string("AT+CWLAP\r\n"); 
    
    uint32_t timeout = 0;
    rx_pos = 0;
    
    // Bucle de espera largo
    while (timeout < TIMEOUT_LONG) { 
        
        // try_read_line ahora se encarga de salvar los datos en RAM
        if (try_read_line()) {
            
            // Si hay línea, la mostramos.
            // AUNQUE tardemos mucho en mostrarla (scroll), 
            // los siguientes datos se acumularán en ring_buffer 
            // en la siguiente llamada a try_read_line/uart_drain.
            if (is_valid_response() || is_terminator()) {
                show_rx_line();
            }
            
            if (rx_pos >= 2 && rx_line[0] == 'O' && rx_line[1] == 'K') break;
            if (rx_pos >= 5 && rx_line[0] == 'E' && rx_line[1] == 'R') break;
            
            rx_pos = 0;
        }
        
        // TRUCO PRO: Si el Z80 está ocioso esperando, 
        // forzamos drenaje constante para mantener el hardware vacío.
        uart_drain_to_buffer();
        
        timeout++;
    }
    
    if (timeout >= TIMEOUT_LONG) {
        main_puts("[Timeout]"); 
        main_newline();
    }
}

static void cmd_info(void) { uart_flush_rx(); uart_send_string("AT+GMR\r\n"); wait_at_response(); }

static void cmd_debug(void) { debug_mode = !debug_mode; current_attr = ATTR_LOCAL; main_puts(debug_mode ? "Debug ON" : "Debug OFF"); main_newline(); }

static void cmd_time(void)
{
    uint16_t timeout;
    uint8_t i, wait;
    uint8_t found = 0;
    
    current_attr = ATTR_LOCAL;
    main_puts("Configuring NTP...");
    main_newline();
    
    // Configure SNTP (timezone 1 = CET for Spain)
    // Using multiple NTP servers for reliability
    uart_flush_hard();
    uart_send_string("AT+CIPSNTPCFG=1,1,\"pool.ntp.org\",\"time.google.com\"\r\n");
    rx_pos = 0;
    timeout = 0;
    while (timeout < 20000) {
        if (try_read_line()) {
            if (debug_mode) { 
                current_attr = ATTR_DEBUG;
                main_puts("CFG:"); main_puts(rx_line); main_newline(); 
            }
            if (rx_pos >= 2 && rx_line[0] == 'O' && rx_line[1] == 'K') break;
            if (rx_pos >= 5 && rx_line[0] == 'E' && rx_line[1] == 'R') break;
            rx_pos = 0;
        }
        timeout++;
    }
    
    // Wait for NTP sync (needs several seconds)
    main_puts("Syncing");
    for (wait = 0; wait < 60; wait++) {
        __asm__("ei");
        __asm__("halt");
        if ((wait % 10) == 0) main_putchar('.');
    }
    main_newline();
    
    // Query time
    main_puts("Getting time...");
    main_newline();
    
    uart_flush_hard();
    uart_send_string("AT+CIPSNTPTIME?\r\n");
    rx_pos = 0;
    timeout = 0;
    
    while (timeout < 30000) {
        if (try_read_line()) {
            // Always show response for debugging
            current_attr = ATTR_DEBUG;
            main_puts("RAW:"); 
            main_puts(rx_line); 
            main_newline();
            
            // +CIPSNTPTIME:Thu Jan 01 00:00:00 1970  (not synced)
            // +CIPSNTPTIME:Fri Dec 27 21:45:30 2024  (synced)
            if (rx_pos > 15 && rx_line[0] == '+' && rx_line[1] == 'C' &&
                rx_line[2] == 'I' && rx_line[3] == 'P' && rx_line[4] == 'S' &&
                rx_line[5] == 'N' && rx_line[6] == 'T' && rx_line[7] == 'P' &&
                rx_line[8] == 'T' && rx_line[9] == 'I' && rx_line[10] == 'M' &&
                rx_line[11] == 'E' && rx_line[12] == ':') {
                
                // Check if it's 1970 (not synced)
                uint8_t is_1970 = 0;
                for (i = rx_pos - 4; i < rx_pos; i++) {
                    if (rx_line[i] == '1' && rx_line[i+1] == '9' && 
                        rx_line[i+2] == '7' && rx_line[i+3] == '0') {
                        is_1970 = 1;
                        break;
                    }
                }
                
                if (!is_1970) {
                    // Find time pattern HH:MM:SS (look for XX:XX:XX)
                    for (i = 13; i < rx_pos - 7; i++) {
                        if (rx_line[i] >= '0' && rx_line[i] <= '2' &&
                            rx_line[i+1] >= '0' && rx_line[i+1] <= '9' &&
                            rx_line[i+2] == ':' &&
                            rx_line[i+3] >= '0' && rx_line[i+3] <= '5' &&
                            rx_line[i+4] >= '0' && rx_line[i+4] <= '9' &&
                            rx_line[i+5] == ':') {
                            // Found HH:MM:SS
                            device_time[0] = rx_line[i];
                            device_time[1] = rx_line[i+1];
                            device_time[2] = ':';
                            device_time[3] = rx_line[i+3];
                            device_time[4] = rx_line[i+4];
                            device_time[5] = 0;
                            found = 1;
                            break;
                        }
                    }
                }
            }
            
            if (rx_pos >= 2 && rx_line[0] == 'O' && rx_line[1] == 'K') break;
            if (rx_pos >= 5 && rx_line[0] == 'E' && rx_line[1] == 'R') break;
            rx_pos = 0;
        }
        timeout++;
    }
    
    uart_flush_rx();
    
    current_attr = ATTR_LOCAL;
    if (found) {
        main_puts("Time set: ");
        main_puts(device_time);
        main_newline();
    } else {
        main_puts("Sync failed (try again in a few seconds)");
        main_newline();
        strcpy(device_time, "--:--");
    }
    
    draw_status_bar();
}

static void cmd_rst(void)
{
    uint8_t i;
    current_attr = ATTR_LOCAL;
    main_puts("Resetting ESP...");
    main_newline();
    
    uart_flush_hard();
    uart_send_string("AT+RST\r\n");
    
    for (i = 0; i < 100; i++) {
        __asm__("ei");
        __asm__("halt");
    }
    
    uart_flush_rx();
    main_puts("Done. Run !IP to check.");
    main_newline();
}

static void cmd_mac(void)
{
    uart_flush_hard();
    uart_send_string("AT+CIPSTAMAC?\r\n");
    wait_at_response();
}

static void cmd_raw(void)
{
    uint8_t c;
    uint16_t timeout;
    
    current_attr = ATTR_LOCAL;
    main_puts("=== RAW MODE (SPACE to exit) ===");
    main_newline();
    
    current_attr = ATTR_DEBUG;
    
    while (1) {
        c = in_inkey();
        if (c == ' ') break;
        
        timeout = 0;
        while (ay_uart_ready() && timeout < 100) {
            c = ay_uart_read();
            if (c >= 32 && c < 127) {
                main_putchar(c);
            } else if (c == 13 || c == 10) {
                main_newline();
            } else {
                main_putchar('<');
                main_putchar("0123456789ABCDEF"[c >> 4]);
                main_putchar("0123456789ABCDEF"[c & 0x0F]);
                main_putchar('>');
            }
            timeout++;
        }
        
        __asm__("ei");
        __asm__("halt");
    }
    
    current_attr = ATTR_LOCAL;
    main_newline();
    main_puts("=== END RAW ===");
    main_newline();
}

static void cmd_connect(void)
{
    uint8_t i;
    char cmd[80];
    uint8_t cwjap_error = 0; // 0: Desconocido, 1:Timeout, 2:Pass, 3:SSID, 4:Fail

    // --- PARSEO (Igual que tenías) ---
    i = 8;
    while (i < line_len && line_buffer[i] == ' ') i++;
    if (i >= line_len) {
        current_attr = ATTR_LOCAL;
        main_puts("Usage: !CONNECT ssid,pass"); main_newline();
        return;
    }
    
    current_attr = ATTR_LOCAL;
    main_puts("Connecting..."); main_newline();
    
    // Construcción del comando AT (Copiado de tu lógica v8)
    strcpy(cmd, "AT+CWJAP=\"");
    uint8_t pos = 10;
    while (i < line_len && line_buffer[i] != ',' && pos < 70) cmd[pos++] = line_buffer[i++];
    if (line_buffer[i] == ',') i++;
    while (i < line_len && line_buffer[i] == ' ') i++; 
    cmd[pos++] = '"'; cmd[pos++] = ','; cmd[pos++] = '"';
    while (i < line_len && pos < 78) cmd[pos++] = line_buffer[i++];
    cmd[pos++] = '"'; cmd[pos++] = '\r'; cmd[pos++] = '\n'; cmd[pos] = 0;
    
    uart_flush_hard();
    uart_send_string(cmd);
    
    // --- NUEVA LÓGICA DE ESPERA ---
    uint32_t timeout = 0;
    rx_pos = 0;
    
    while (timeout < TIMEOUT_LONG) { // Timeout largo
        if (try_read_line()) {
            
            // 1. CAPTURAR ERROR ESPECÍFICO
            // El ESP envía "+CWJAP:1" (o 2, 3, 4) antes de enviar "FAIL"
            if (rx_pos >= 7 && rx_line[0] == '+' && rx_line[1] == 'C' && rx_line[2] == 'W') {
                // Chequeo rápido de string "+CWJAP:"
                if (rx_line[6] == ':' && rx_line[7] >= '0' && rx_line[7] <= '4') {
                    cwjap_error = rx_line[7] - '0';
                }
            }
            
            // Mostrar respuesta en pantalla (Debug visual)
            if (is_valid_response() || is_terminator()) show_rx_line();
            
            // 2. ÉXITO
            if (rx_pos >= 2 && rx_line[0] == 'O' && rx_line[1] == 'K') {
                current_attr = ATTR_LOCAL;
                main_puts("Success!"); main_newline();
                check_connection(); // Actualiza IP y Status Bar
                // IMPORTANTE: Forzar refresco visual inmediato
                draw_status_bar(); 
                return;
            }
            
            // 3. FALLO (FAIL o ERROR)
            if ((rx_pos >= 4 && rx_line[0] == 'F' && rx_line[1] == 'A') || 
                (rx_pos >= 5 && rx_line[0] == 'E' && rx_line[1] == 'R')) {
                
                current_attr = ATTR_LOCAL;
                main_newline();
                main_puts("Connection Failed:"); main_newline();
                
                // Explicación detallada
                switch(cwjap_error) {
                    case 1: main_puts("> Timeout / Router busy"); break;
                    case 2: main_puts("> Wrong Password"); break;
                    case 3: main_puts("> SSID not found"); break;
                    case 4: main_puts("> Connection Failed"); break;
                    default: main_puts("> Unknown Error"); break;
                }
                main_newline();
                return;
            }
            rx_pos = 0;
        }
        timeout++;
    }
    
    current_attr = ATTR_LOCAL;
    main_puts("Terminal Timeout."); main_newline();
}

static void cmd_disconnect(void)
{
    current_attr = ATTR_LOCAL;
    main_puts("Disconnecting...");
    main_newline();
    
    uart_flush_hard();
    uart_send_string("AT+CWQAP\r\n");
    wait_at_response();
    
    strcpy(device_ip, "---");
    strcpy(device_ssid, "---");
    device_rssi = 0;
    connection_status = 0;
    draw_status_bar();
}

static void cmd_ping(void)
{
    uint8_t i;
    char cmd[40];
    
    // Parse: !PING ip
    i = 5;
    while (i < line_len && line_buffer[i] == ' ') i++;
    
    if (i >= line_len) {
        // Default ping
        strcpy(cmd, "AT+PING=\"8.8.8.8\"\r\n");
    } else {
        strcpy(cmd, "AT+PING=\"");
        uint8_t pos = 9;
        while (i < line_len && pos < 35) {
            cmd[pos++] = line_buffer[i++];
        }
        cmd[pos++] = '"';
        cmd[pos++] = '\r';
        cmd[pos++] = '\n';
        cmd[pos] = 0;
    }
    
    current_attr = ATTR_LOCAL;
    main_puts("Pinging...");
    main_newline();
    
    uart_flush_hard();
    uart_send_string(cmd);
    wait_at_response();
}

static void cmd_baud(void)
{
    uint8_t i;
    char rate[10];
    
    // Parse: !BAUD rate
    i = 5;
    while (i < line_len && line_buffer[i] == ' ') i++;
    
    if (i >= line_len) {
        current_attr = ATTR_LOCAL;
        main_puts("Usage: !BAUD rate (9600,19200,38400,115200)");
        main_newline();
        return;
    }
    
    // Extract rate
    uint8_t j = 0;
    while (i < line_len && j < 9) {
        rate[j++] = line_buffer[i++];
    }
    rate[j] = 0;
    
    current_attr = ATTR_LOCAL;
    main_puts("Setting baud to ");
    main_puts(rate);
    main_puts("...");
    main_newline();
    
    char cmd[40];
    strcpy(cmd, "AT+UART_CUR=");
    strcat(cmd, rate);
    strcat(cmd, ",8,1,0,0\r\n");
    
    uart_flush_hard();
    uart_send_string(cmd);
    
    // Note: After this, communication may break if Z80 side doesn't match
    main_puts("Note: Z80 UART unchanged. Restart to take effect.");
    main_newline();
}

// Help screen with 2 pages

static void show_help_page1(void)
{
    clear_zone(MAIN_START, MAIN_LINES, PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 0, 13, "======== ESPAT-ZX HELP (1/2) ========", PAPER_BLUE | INK_WHITE | BRIGHT);
    
    print_str64(MAIN_START + 2, 2, "!CONNECT s,p", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 2, 16, "Connect to WiFi network", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 3, 2, "!DISCONNECT", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 3, 16, "Disconnect from WiFi", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 4, 2, "!PING [ip]", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 4, 16, "Ping host (default 8.8.8.8)", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 5, 2, "!SCAN", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 5, 16, "Scan WiFi networks", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 6, 2, "!IP", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 6, 16, "Refresh connection status", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 7, 2, "!TIME", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 7, 16, "Sync time from NTP server", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 8, 2, "!INFO", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 8, 16, "ESP firmware version", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 9, 2, "!MAC", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 9, 16, "Show MAC address", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 11, 2, "UP/DOWN", PAPER_BLUE | INK_GREEN | BRIGHT);
    print_str64(MAIN_START + 11, 16, "Command history", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 13, 2, "Example:", PAPER_BLUE | INK_CYAN);
    print_str64(MAIN_START + 14, 4, "!CONNECT MyWiFi,MyPassword123", PAPER_BLUE | INK_WHITE);
    
    // Footer centrado
    print_str64(MAIN_START + 16, 18, "-- Press SPACE for page 2 --", PAPER_BLUE | INK_WHITE | BRIGHT);
}


static void show_help_page2(void)
{
    clear_zone(MAIN_START, MAIN_LINES, PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 0, 13, "======== ESPAT-ZX HELP (2/2) ========", PAPER_BLUE | INK_WHITE | BRIGHT);
    
    print_str64(MAIN_START + 2, 2, "!RST", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 2, 16, "Reset ESP module", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 3, 2, "!RAW", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 3, 16, "Raw traffic monitor", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 4, 2, "!BAUD rate", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 4, 16, "Change ESP baud rate", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 5, 2, "!DEBUG", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 5, 16, "Toggle debug output", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 6, 2, "!CLS", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 6, 16, "Clear screen", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 7, 2, "!ABOUT", PAPER_BLUE | INK_YELLOW | BRIGHT);
    print_str64(MAIN_START + 7, 16, "Show credits", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 9, 2, "Or type AT commands directly:", PAPER_BLUE | INK_CYAN);
    print_str64(MAIN_START + 10, 4, "AT+CWJAP=\"SSID\",\"password\"", PAPER_BLUE | INK_WHITE);
    print_str64(MAIN_START + 11, 4, "AT+CIPSTART=\"TCP\",\"ip\",port", PAPER_BLUE | INK_WHITE);
    
    print_str64(MAIN_START + 13, 2, "Status bar shows:", PAPER_BLUE | INK_CYAN);
    print_str64(MAIN_START + 14, 4, "IP | SSID | RSSI | Time | Signal | Status", PAPER_BLUE | INK_WHITE);
    
    // Footer con instrucción de volver
    print_str64(MAIN_START + 16, 16, "-- 'B' Back | Any Key Exit --", PAPER_BLUE | INK_WHITE | BRIGHT);
}

static void cmd_about(void)
{
    clear_zone(MAIN_START, MAIN_LINES, PAPER_BLACK | INK_WHITE);
    
    // Título (Centrado)
    print_str64(MAIN_START + 1, 28, "ESPAT-ZX", PAPER_BLACK | INK_CYAN | BRIGHT);
    print_str64(MAIN_START + 2, 18, "AT Terminal for ZX Spectrum", PAPER_BLACK | INK_WHITE);
    print_str64(MAIN_START + 3, 29, "v1.0", PAPER_BLACK | INK_CYAN);
    print_str64(MAIN_START + 5, 16, "(c) 2025 M. Ignacio Monge Garcia", PAPER_BLACK | INK_YELLOW | BRIGHT);
    
    // Créditos Base
    print_str64(MAIN_START + 8, 18, "Based on 'esp-terminal' by:", PAPER_BLACK | INK_WHITE);
    print_str64(MAIN_START + 9, 18, "Vasily Khoruzhick (anarsoul)", PAPER_BLACK | INK_GREEN | BRIGHT);
    
    // Créditos Adicionales
    print_str64(MAIN_START + 11, 22, "Includes code from:", PAPER_BLACK | INK_WHITE);
    print_str64(MAIN_START + 12, 18, "BridgeZX & NetManZX drivers", PAPER_BLACK | INK_GREEN | BRIGHT);
    
    // Footer
    print_str64(MAIN_START + 16, 18, "-- Press any key to exit --", PAPER_BLACK | INK_WHITE | BRIGHT);
    
    while (in_inkey() != 0) { __asm__("halt"); }
    while (in_inkey() == 0) { __asm__("halt"); }
    while (in_inkey() != 0) { __asm__("halt"); }
    
    clear_zone(MAIN_START, MAIN_LINES, ATTR_MAIN_BG);
    main_line = MAIN_START;
    main_col = 0;
    current_attr = ATTR_LOCAL;
    main_puts("Ready.");
    main_newline();
}

static void show_help_screen(void)
{
    uint8_t key;
    uint8_t current_page = 1;
    
    while (current_page != 0) {
        // Dibujar página actual
        if (current_page == 1) show_help_page1();
        else show_help_page2();
        
        // Esperar tecla
        while (in_inkey() != 0) { __asm__("halt"); }
        while ((key = in_inkey()) == 0) { __asm__("halt"); }
        while (in_inkey() != 0) { __asm__("halt"); }
        
        // Lógica de navegación
        if (current_page == 1) {
            if (key == ' ') current_page = 2; // Espacio -> Pag 2
            else current_page = 0;            // Otra -> Salir
        } else {
            if (key == 'b' || key == 'B') current_page = 1; // 'B' -> Volver a Pag 1
            else current_page = 0;                          // Otra -> Salir
        }
    }
    
    // Salir
    clear_zone(MAIN_START, MAIN_LINES, ATTR_MAIN_BG);
    main_line = MAIN_START;
    main_col = 0;
    current_attr = ATTR_LOCAL;
    main_puts("Ready.");
    main_newline();
}

static void cmd_help(void) { show_help_screen(); }

static uint8_t process_local_command(void)
{
    if (line_len == 0 || line_buffer[0] != '!') return 0;
    if (cmd_match("!CLS")) { cmd_cls(); return 1; }
    if (cmd_match("!IP")) { cmd_ip(); return 1; }
    if (cmd_match("!SCAN")) { cmd_scan(); return 1; }
    if (cmd_match("!INFO")) { cmd_info(); return 1; }
    if (cmd_match("!TIME")) { cmd_time(); return 1; }
    if (cmd_match("!MAC")) { cmd_mac(); return 1; }
    if (cmd_match("!RST")) { cmd_rst(); return 1; }
    if (cmd_match("!RAW")) { cmd_raw(); return 1; }
    if (cmd_match("!DEBUG")) { cmd_debug(); return 1; }
    if (cmd_match("!CONNECT")) { cmd_connect(); return 1; }
    if (cmd_match("!DISCONNECT")) { cmd_disconnect(); return 1; }
    if (cmd_match("!PING")) { cmd_ping(); return 1; }
    if (cmd_match("!BAUD")) { cmd_baud(); return 1; }
    if (cmd_match("!HELP") || cmd_match("!?")) { cmd_help(); return 1; }
    if (cmd_match("!ABOUT")) { cmd_about(); return 1; }
    return 0;
}

// ============================================================
// KEYBOARD
// ============================================================

#define KEY_UP    11
#define KEY_DOWN  10
#define KEY_LEFT  8
#define KEY_RIGHT 9
#define KEY_BACKSPACE 12

static uint8_t last_k = 0;
static uint16_t repeat_timer = 0;
static uint8_t debounce_zero = 0; // Escudo anti-cero

static uint8_t read_key(void)
{
    uint8_t k = in_inkey();

    if (k == 0) {
        last_k = 0;
        repeat_timer = 0;
        if (debounce_zero > 0) debounce_zero--; 
        return 0;
    }

    if (k == '0' && debounce_zero > 0) {
        debounce_zero--; 
        return 0;        
    }

    // NUEVA TECLA - devolver inmediatamente
    if (k != last_k) {
        last_k = k;
        
        // Delay inicial en FRAMES (50 fps)
        // 10 frames = 200ms, 15 frames = 300ms
        if (k == KEY_BACKSPACE) {
            repeat_timer = 12;  // ~240ms antes de auto-repeat
        } else if (k == KEY_LEFT || k == KEY_RIGHT) {
            repeat_timer = 15;  // ~300ms
        } else {
            repeat_timer = 20;  // ~400ms para texto
        }
        
        if (k == KEY_BACKSPACE) debounce_zero = 8; 
        else debounce_zero = 0;

        return k;
    }

    // MANTENIENDO TECLA - auto-repeat
    if (k == KEY_BACKSPACE) debounce_zero = 8;

    if (repeat_timer > 0) {
        repeat_timer--;
        return 0;
    } else {
        // Velocidad de repetición en frames
        if (k == KEY_BACKSPACE) {
            repeat_timer = 1;   // Cada frame = 20ms = 50 chars/seg
            return k;
        }
        if (k == KEY_LEFT || k == KEY_RIGHT) {
            repeat_timer = 2;   // ~25 movimientos/seg
            return k;
        }
        if (k == KEY_UP || k == KEY_DOWN) {
            repeat_timer = 5;   // ~10 items/seg
            return k;
        }
        return 0;
    }
}

// ============================================================
// INIT AND MAIN
// ============================================================

static void draw_banner(void)
{
    clear_line(BANNER_START, ATTR_BANNER);
    print_str64(BANNER_START, 2, "ESPAT-ZX: AT Terminal for ZX Spectrum", ATTR_BANNER);
    print_str64(BANNER_START, 60, "v1.0", ATTR_BANNER);
}

static void init_screen(void)
{
    uint8_t i;
    zx_border(INK_BLACK);
    for (i = 0; i < 24; i++) clear_line(i, PAPER_BLACK);
    
    clear_zone(BANNER_START, 1, ATTR_BANNER);
    clear_line(1, ATTR_MAIN_BG);
    clear_zone(MAIN_START, MAIN_LINES, ATTR_MAIN_BG);
    clear_line(19, ATTR_MAIN_BG);
    clear_line(STATUS_LINE, ATTR_STATUS);
    // Dejamos la línea 21 negra como separador (INPUT_START es 22)
    clear_line(21, PAPER_BLACK | INK_BLACK); 
    clear_zone(INPUT_START, INPUT_LINES, ATTR_INPUT_BG);
    
    draw_banner();
    draw_status_bar();
    
    main_line = MAIN_START;
    main_col = 0;
    
    // Inicializar variables de input MANUALMENTE sin activar el redibujado
    line_len = 0; 
    line_buffer[0] = 0;
    cursor_pos = 0;
    
    // Solo dibujamos el prompt estático, sin cursor
    print_char64(INPUT_START, 0, '>', ATTR_PROMPT);
}

// Definiciones de teclas de navegación

void main(void)
{
    uint8_t c;
    uint16_t refresh_counter = 0;
    
    init_screen();
    smart_init();
    
    terminal_ready = 1;

    // Mensaje de bienvenida actualizado
    current_attr = ATTR_LOCAL;
    main_puts("Ready. Type !HELP, !ABOUT or AT cmds"); // <-- AÑADIDO !ABOUT
    main_newline();
    
    // ¡AHORA SÍ! Activamos el cursor por primera vez
    redraw_input_from(0);
    while (1) {
        __asm__("ei");
        __asm__("halt");  // 50 fps timing base
        
        // IDLE: Descartar datos basura del UART directamente (máx 2 bytes/frame)
        // Esto mantiene el UART limpio sin bloquear el teclado
        if (ay_uart_ready()) { ay_uart_read(); }
        if (ay_uart_ready()) { ay_uart_read(); }
        
        // Auto-refresh de RSSI (~cada 40 segundos a 50fps)
        refresh_counter++;
        if (refresh_counter >= 2000 && connection_status == 1) {
            refresh_counter = 0;
            
            set_input_busy(1);
            
            uint8_t old_debug = debug_mode;
            debug_mode = 0;
            get_ssid_rssi();
            debug_mode = old_debug;
            draw_status_bar();
            
            set_input_busy(0);
        }
        
        c = read_key();
        if (c == 0) continue;
        
        refresh_counter = 0;
        
        // --- 1. NAVEGACIÓN SEGURA ---
        
        // Flechas ARRIBA/ABAJO -> Exclusivas para Historial
        if (c == KEY_UP) { 
            uint8_t prev_len = line_len;
            history_nav_up();
            cursor_pos = line_len;
            // Si la línea anterior era más larga, limpiar residuos (incluido cursor antiguo)
            if (prev_len >= line_len) {
                uint8_t i;
                for (i = line_len; i <= prev_len; i++) {
                    uint16_t abs_pos = i + 2;
                    uint8_t row = INPUT_START + (abs_pos / SCREEN_COLS);
                    uint8_t col = abs_pos % SCREEN_COLS;
                    if (row <= INPUT_END) {
                        print_char64(row, col, ' ', ATTR_INPUT_BG);
                    }
                }
            }
            redraw_input_from(0);
        }
        else if (c == KEY_DOWN) { 
            uint8_t prev_len = line_len;
            history_nav_down();
            cursor_pos = line_len;
            // Si la línea anterior era más larga, limpiar residuos (incluido cursor antiguo)
            if (prev_len >= line_len) {
                uint8_t i;
                for (i = line_len; i <= prev_len; i++) {
                    uint16_t abs_pos = i + 2;
                    uint8_t row = INPUT_START + (abs_pos / SCREEN_COLS);
                    uint8_t col = abs_pos % SCREEN_COLS;
                    if (row <= INPUT_END) {
                        print_char64(row, col, ' ', ATTR_INPUT_BG);
                    }
                }
            }
            redraw_input_from(0);
        }
        
        // Flechas IZQ/DER -> Exclusivas para Cursor
        else if (c == KEY_LEFT) { 
            input_left(); 
        }
        else if (c == KEY_RIGHT) { 
            input_right(); 
        }
        
        // --- 2. EDICIÓN ---
        
        else if (c == 12) { // Backspace (Delete)
            input_backspace(); 
        }
        else if (c == 13) { // ENTER
            if (line_len > 0) {
                // Copiamos el buffer
                char cmd_copy[LINE_BUFFER_SIZE];
                memcpy(cmd_copy, line_buffer, line_len + 1);
                
                // Guardamos en historial
                history_add(cmd_copy, line_len);
                
                // Feedback visual en pantalla
                current_attr = ATTR_USER;
                main_puts("> ");
                main_puts(cmd_copy);
                main_newline();
                
                // Limpiamos la línea de input inmediatamente
                input_clear(); 
                
                // --- AQUÍ ESTÁ EL TRUCO DEL CURSOR ---
                set_input_busy(1); // 1. Ocultamos cursor (el input ya está limpio, oculta el prompt '>')
                                   //    Ojo: input_clear ya dibujó el prompt, así que esto ocultará
                                   //    el cursor que parpadea sobre el primer carácter vacío.

                if (cmd_copy[0] == '!') {
                    strcpy(line_buffer, cmd_copy);
                    line_len = strlen(line_buffer);
                    
                    if (!process_local_command()) {
                        current_attr = ATTR_LOCAL;
                        main_puts("Unknown command");
                        main_newline();
                    }
                    // Limpiamos tras procesar comando local
                    line_len = 0; line_buffer[0] = 0;
                } else {
                    // Ejecutamos comando AT (Bloqueante)
                    execute_raw_at_command(cmd_copy);
                }
                
                draw_status_bar();
                
                // 2. Restauramos cursor al terminar todo
                set_input_busy(0); 
            }
        }
        // --- 3. TEXTO ---
        else if (c >= 32 && c <= 126) { 
            input_add_char(c);
        }
    }
}