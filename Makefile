TARGET:= ubnt-rf-env

RFENV_SRCS:= $(wildcard *.c)
RFENV_OBJS := $(patsubst %.c,%.o,$(RFENV_SRCS))
RFENV_DEPS := $(patsubst %.c,%.d,$(RFENV_SRCS))

-include $(RFENV_DEPS)

.PHONY: all clean

%.o: %.c
	$(info GEN $@)
	@$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -MMD $(COPTS) -c $< -o $@

LD_LIBS:= -lm -ljansson -lubnt

$(TARGET): $(RFENV_OBJS)
	$(CC) $^ $(LDFLAGS) $(LD_LIBS) -o $@

all: $(RFENV_DEPS)
	@$(MAKE) $(TARGET)

clean:
	-rm -f $(TARGET) $(RFENV_OBJS) $(RFENV_DEPS)
