LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= mobibench.c
LOCAL_MODULE:= mobibench

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../android $(call include-path-for, system-core)/cutils

LOCAL_SHARED_LIBRARIES := libsqlite \
            libicuuc \
            libicui18n \
            libutils

ifneq ($(TARGET_ARCH),arm)
LOCAL_LDLIBS += -lpthread -ldl
endif

LOCAL_CFLAGS += 

LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)

LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
