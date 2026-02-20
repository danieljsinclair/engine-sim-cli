.PHONY: all clean rebuild

all:
	@$(MAKE) -C build

clean:
	@$(MAKE) -C build clean

rebuild: clean all
