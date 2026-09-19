#pragma once
#include <windows.h>
#include <string>

// Include standard JNI header
#include <jni.h>

class JavaDetector {
public:
    // Initialize the detector - call once at startup
    static bool initialize();
    
    // Check if chat screen is currently open
    static bool isChatScreenOpen();
    
    // Cleanup
    static void shutdown();
    
    // Check if initialized
    static bool isInitialized() { return jniInitialized; }

    // True when full JNI detection chain is available (Minecraft instance +
    // currentScreen field + GuiChat class all resolved). When false, callers
    // should fall back to version-independent detection (e.g. cursor visibility).
    static bool isDetectionActive() {
        return jniInitialized && minecraftInstance && currentScreenField && guiChatClass;
    }

    // F1 HUD-hidden state: reads options.hudHidden via JNI. Falls back to
    // local key tracking in the caller when unavailable.
    static bool isHudHidden();
    static bool isHudDetectionActive() {
        return jniInitialized && minecraftInstance && optionsField && hudHiddenField;
    }

    // In-game state: true while a world is loaded (minecraft.world/level != null).
    // False on the title screen / server list / disconnect screens.
    static bool isInGame();
    static bool isInGameDetectionActive() {
        return jniInitialized && minecraftInstance && levelField;
    }

private:
    // JNI related
    static JavaVM* jvm;
    static JNIEnv* env;
    static bool jniInitialized;
    static bool threadAttached;
    
    // Minecraft related
    static jobject minecraftInstance;
    static jfieldID currentScreenField;
    
    // GuiChat class references
    static jclass guiChatClass;

    // HUD-hidden (F1) detection: options field on Minecraft + hudHidden field
    static jfieldID optionsField;
    static jfieldID hudHiddenField;
    static bool resolveHudFields();

    // In-game detection: world/level field on the Minecraft instance
    static jfieldID levelField;
    static bool resolveLevelField();
    
    // Helper methods
    static bool findJVM();
    static bool attachThread();
    static bool findMinecraftInstance();
    static bool findCurrentScreenField();
    static bool findGuiChatClass();
    static bool isGuiChatInstance(jobject screen);
    
    // Cleanup on failure
    static void cleanup();
};
