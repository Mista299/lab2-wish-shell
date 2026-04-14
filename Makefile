# ─────────────────────────────────────────────────────────────────────────────
# Makefile — wish (Wisconsin Shell)
# Laboratorio 2 · Sistemas Operativos · Universidad de Antioquia · 2026-1
# ─────────────────────────────────────────────────────────────────────────────

# Compilador y flags
CC      = gcc
# -Wall    : activar todos los warnings comunes
# -Wextra  : warnings extra (variables no usadas, parámetros sin usar, etc.)
# -g       : incluir información de depuración (para gdb/valgrind)
# -std=c99 : usar el estándar C99 (permite declarar variables en medio de funciones)
CFLAGS  = -Wall -Wextra -g -std=c99

# Nombre del ejecutable final
TARGET  = wish

# Archivos fuente (agregar más si divides en varios .c)
SRCS    = wish.c

# Archivos objeto generados desde los fuentes
OBJS    = $(SRCS:.c=.o)

# ─── Regla principal ──────────────────────────────────────────────────────────
# make  →  compila wish
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)
	@echo "✓ Compilado exitosamente: ./$(TARGET)"

# ─── Compilar archivos .c → .o ───────────────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ─── Limpiar archivos generados ───────────────────────────────────────────────
# make clean  →  elimina ejecutable y objetos
clean:
	rm -f $(TARGET) $(OBJS)
	@echo "✓ Limpieza completada"

# ─── Recompilar desde cero ────────────────────────────────────────────────────
# make re  →  limpia y vuelve a compilar
re: clean all

# ─── Ejecutar en modo interactivo ─────────────────────────────────────────────
# make run  →  ./wish
run: $(TARGET)
	./$(TARGET)

# ─── Revisar memory leaks con valgrind ───────────────────────────────────────
# make valgrind  →  corre wish bajo valgrind en modo interactivo
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --verbose \
	         ./$(TARGET)

# ─── Pruebas rápidas ──────────────────────────────────────────────────────────
# make test  →  corre un conjunto básico de pruebas
test: $(TARGET)
	@echo "=== Prueba 01: comando básico ==="
	@echo "ls /tmp" | ./$(TARGET)

	@echo ""
	@echo "=== Prueba 02: built-in exit ==="
	@echo "exit" | ./$(TARGET); echo "exit code: $$?"

	@echo ""
	@echo "=== Prueba 03: exit con args (debe dar error) ==="
	@echo "exit ahora" | ./$(TARGET)

	@echo ""
	@echo "=== Prueba 04: chd y pwd ==="
	@printf "chd /tmp\npwd\n" | ./$(TARGET)

	@echo ""
	@echo "=== Prueba 05: comando no existente ==="
	@echo "comandoxyz" | ./$(TARGET)

	@echo ""
	@echo "=== Prueba 06: redirección ==="
	@echo "echo hola > /tmp/wish_test.txt" | ./$(TARGET)
	@cat /tmp/wish_test.txt

	@echo ""
	@echo "=== Prueba 07: paralelos ==="
	@echo "echo A & echo B & echo C" | ./$(TARGET)

	@echo ""
	@echo "=== Prueba 08: route y búsqueda ==="
	@printf "route /bin\nls /tmp\n" | ./$(TARGET)

	@echo ""
	@echo "=== Prueba 09: argc > 2 ==="
	@./$(TARGET) a b c; echo "exit code: $$?"

	@echo ""
	@echo "=== Prueba 10: batch mode ==="
	@printf "echo batch OK\npwd\nexit\n" > /tmp/wish_batch_test.txt
	@./$(TARGET) /tmp/wish_batch_test.txt

	@echo ""
	@echo "=== Todas las pruebas básicas ejecutadas ==="

# Declarar targets que no son archivos reales
.PHONY: all clean re run valgrind test
