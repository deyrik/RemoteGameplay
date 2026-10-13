CXX = g++
GST_CFLAGS = $(shell pkg-config --cflags gstreamer-1.0 gstreamer-video-1.0)
GST_LIBS   = $(shell pkg-config --libs gstreamer-1.0 gstreamer-video-1.0)

# Por padrão, compila para Produção/Deploy (-O3, sem logs pesados de terminal)
MODE_FLAGS ?= -O3

CXXFLAGS = -Wall -std=c++17 -pthread $(MODE_FLAGS) $(GST_CFLAGS)

ifeq ($(OS),Windows_NT)
    LIBS = $(GST_LIBS) -lpthread -lws2_32 -lxinput -lgdi32 -luser32
    RM = del /Q
    EXT = .exe
else
    LIBS = $(GST_LIBS) -lpthread -lX11 -lXtst -lXi
    RM = rm -f
    EXT =
endif

# Compilação Oficial (Deploy): make
all: server client

# Compilação de Desenvolvedor (Ativa logs DBG_LOG e símbolos de debug): make dev
dev: MODE_FLAGS = -g -O0 -DDEBUG_INPUT
dev: clean all

server: server/main_server.cpp
	$(CXX) $(CXXFLAGS) server/main_server.cpp -o host_server$(EXT) $(LIBS)

client: client/main_client.cpp
	$(CXX) $(CXXFLAGS) client/main_client.cpp -o game_client$(EXT) $(LIBS)

clean:
	$(RM) host_server$(EXT) game_client$(EXT)