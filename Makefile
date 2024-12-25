CC := gcc
CFLAGS := -shared -fpic
SOURCES := dllmain.c dosfs-1.03/dosfs.c
TARGET := jacknife.wcx

ifdef GUI
CFLAGS += $(shell pkg-config --cflags gtk+-2.0) -DGUI_CODE
endif

$(TARGET): $(SOURCES)
	$(CC) $(SOURCES) $(CFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)

