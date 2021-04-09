 CC = gcc
TARGET_EXEC ?= cantv

BUILD_DIR ?= ./build
TARGET_DIR ?= ./
SRC_DIR ?= ./src

SRCS := $(shell find ./src -name '*.c')

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

INC_DIRS := $(shell find $(SRC_DIR) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))
INC_FLAGS := $(INC_FLAGS) `pkg-config glib-2.0 --cflags`
INC_FLAGS := $(INC_FLAGS) `pkg-config jansson --cflags`

CFLAGS := -g $(USER_DEFINES) -MMD -MP -fPIC -Wno-psabi -fPIE -fstack-protector $(INC_FLAGS) -D_XOPEN_SOURCE=600 -D_DEFAULT_SOURCE
LDFLAGS := -pie -Wl,-z,now -rdynamic 

CFLAGS := $(CFLAGS) -g -std=c99 

LDFLAGS := $(LDFLAGS) -lpthread `pkg-config glib-2.0 --libs`
LDFLAGS := $(LDFLAGS) `pkg-config jansson --libs`
LDFLAGS := $(LDFLAGS) `pkg-config libcurl --libs`
LDFLAGS := $(LDFLAGS) -lm -ldl -lm -luuid 

$(TARGET_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# assembly
$(BUILD_DIR)/%.s.o: %.s
	$(MKDIR_P) $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# c source
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	$(MKDIR_P) $(dir $@)
	$(CXX) $(CPPFLAGS) $(COMPILEOPTIONS) -c $< -o $@

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)
	$(RM) -r $(TARGET_DIR)/$(TARGET_EXEC)

-include $(DEPS)

MKDIR_P ?= mkdir -p
