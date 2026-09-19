#include "JavaDetector.h"
#include <string>
#include <vector>
#include <algorithm>

// Static member initialization
JavaVM* JavaDetector::jvm = nullptr;
JNIEnv* JavaDetector::env = nullptr;
bool JavaDetector::jniInitialized = false;
bool JavaDetector::threadAttached = false;
jobject JavaDetector::minecraftInstance = nullptr;
jfieldID JavaDetector::currentScreenField = nullptr;
jclass JavaDetector::guiChatClass = nullptr;
jfieldID JavaDetector::optionsField = nullptr;
jfieldID JavaDetector::hudHiddenField = nullptr;
jfieldID JavaDetector::levelField = nullptr;

// Class/field name variants for different versions
static const char* minecraftClassNames[] = {
    "net/minecraft/client/Minecraft",
    "ave",  // 1.8.9 obfuscated
    "bib",  // 1.12.2 obfuscated
    "net/minecraft/client/MinecraftClient", // Fabric/Yarn
    "net/minecraft/class_310",              // Fabric intermediary (stable across versions)
    "net/minecraft/client/Minecraft",       // Mojmap
    nullptr
};

static const char* instanceFieldNames[] = {
    "theMinecraft",
    "instance",
    "INSTANCE",  // newer Yarn
    "field_1700",  // Fabric intermediary static instance
    "field_71432_P",
    nullptr
};

static const char* currentScreenFieldNames[] = {
    "currentScreen",
    "field_1755",  // Fabric intermediary
    "field_71462_r",
    "field_147125_j",
    nullptr
};

static const char* guiChatClassNames[] = {
    "net/minecraft/client/gui/GuiChat",
    "bfl",  // 1.8.9 obfuscated
    "bfh",
    "net/minecraft/client/gui/screen/ChatScreen", // Fabric/Yarn
    "net/minecraft/class_408",                    // Fabric intermediary
    "net/minecraft/client/gui/screens/ChatScreen", // Mojmap 1.20.5+
    nullptr
};

// Resolve a class by trying the bootstrap loader first, then the current
// thread's context class loader. Fabric/Forge load Minecraft classes through
// their own class loaders (e.g. KnotClassLoader), which the plain JNI
// FindClass from an attached native thread cannot see - this makes class
// resolution work on those environments (e.g. MC 1.21.4 Fabric).
static jclass findClassEx(JNIEnv* env, const char* slashName) {
    jclass cls = env->FindClass(slashName);
    if (cls) return cls;
    env->ExceptionClear();

    jclass threadClass = env->FindClass("java/lang/Thread");
    if (!threadClass) { env->ExceptionClear(); return nullptr; }
    jmethodID currentThread = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
    if (!currentThread) { env->ExceptionClear(); env->DeleteLocalRef(threadClass); return nullptr; }
    jobject thread = env->CallStaticObjectMethod(threadClass, currentThread);
    if (!thread) { env->ExceptionClear(); env->DeleteLocalRef(threadClass); return nullptr; }

    jmethodID getLoader = env->GetMethodID(threadClass, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = (getLoader && thread) ? env->CallObjectMethod(thread, getLoader) : nullptr;
    env->ExceptionClear();
    if (!loader) {
        env->DeleteLocalRef(thread); env->DeleteLocalRef(threadClass);
        return nullptr;
    }

    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = loaderClass ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    if (!loadClass) { env->ExceptionClear(); /* cleanup */ env->DeleteLocalRef(loader); env->DeleteLocalRef(thread); env->DeleteLocalRef(threadClass); if (loaderClass) env->DeleteLocalRef(loaderClass); return nullptr; }

    // convert slash name to binary name (net/minecraft/... -> net.minecraft...)
    std::string binary(slashName);
    std::replace(binary.begin(), binary.end(), '/', '.');

    jstring name = env->NewStringUTF(binary.c_str());
    jobject result = env->CallObjectMethod(loader, loadClass, name);
    env->ExceptionClear();

    env->DeleteLocalRef(name);
    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(thread);
    env->DeleteLocalRef(threadClass);
    if (loaderClass) env->DeleteLocalRef(loaderClass);

    return (jclass)result; // caller may wrap in NewGlobalRef
}

void JavaDetector::cleanup() {
    if (threadAttached && jvm) {
        jvm->DetachCurrentThread();
        threadAttached = false;
    }
    env = nullptr;
    minecraftInstance = nullptr;
    currentScreenField = nullptr;
    guiChatClass = nullptr;
    optionsField = nullptr;
    hudHiddenField = nullptr;
    levelField = nullptr;
    jniInitialized = false;
}

bool JavaDetector::findJVM() {
    // Use JNI_GetCreatedJavaVMs to find existing JVM
    HMODULE jvmDll = GetModuleHandleA("jvm.dll");
    if (!jvmDll) {
        OutputDebugStringA("[WatermarkDLL] jvm.dll not found in process\n");
        return false;
    }
    
    // Get JNI_GetCreatedJavaVMs function
    typedef jint(JNICALL *JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);
    JNI_GetCreatedJavaVMs_t JNI_GetCreatedJavaVMs = 
        (JNI_GetCreatedJavaVMs_t)GetProcAddress(jvmDll, "JNI_GetCreatedJavaVMs");
    
    if (!JNI_GetCreatedJavaVMs) {
        OutputDebugStringA("[WatermarkDLL] JNI_GetCreatedJavaVMs function not found\n");
        return false;
    }
    
    // Get existing JVM
    jsize vmCount = 0;
    jint result = JNI_GetCreatedJavaVMs(&jvm, 1, &vmCount);
    if (result != JNI_OK || vmCount == 0 || !jvm) {
        OutputDebugStringA("[WatermarkDLL] No JVM found or error getting JVM\n");
        return false;
    }
    
    OutputDebugStringA("[WatermarkDLL] JVM found successfully\n");
    return true;
}

bool JavaDetector::attachThread() {
    if (!jvm) return false;
    
    // Check if already attached
    JNIEnv* existingEnv = nullptr;
    jint status = jvm->GetEnv((void**)&existingEnv, JNI_VERSION_1_8);
    if (status == JNI_OK && existingEnv) {
        env = existingEnv;
        threadAttached = true;
        OutputDebugStringA("[WatermarkDLL] Thread already attached to JVM\n");
        return true;
    }
    
    // Attach current thread
    JavaVMAttachArgs args;
    args.version = JNI_VERSION_1_8;
    args.name = (char*)"WatermarkDLL";
    args.group = nullptr;
    
    jint result = jvm->AttachCurrentThread((void**)&env, &args);
    if (result != JNI_OK || !env) {
        OutputDebugStringA("[WatermarkDLL] Failed to attach thread to JVM\n");
        return false;
    }
    
    threadAttached = true;
    OutputDebugStringA("[WatermarkDLL] Thread attached to JVM successfully\n");
    return true;
}

bool JavaDetector::findMinecraftInstance() {
    if (!env) return false;
    
    // Try each class name
    for (int i = 0; minecraftClassNames[i]; i++) {
        jclass clazz = findClassEx(env, minecraftClassNames[i]);
        if (!clazz) {
            continue;
        }
        
        OutputDebugStringA(("[WatermarkDLL] Found Minecraft class: " + std::string(minecraftClassNames[i]) + "\n").c_str());
        
        // Try each instance field name
        for (int j = 0; instanceFieldNames[j]; j++) {
            // Try different signatures
            const char* signatures[] = {
                ("L" + std::string(minecraftClassNames[i]) + ";").c_str(),
                "Lnet/minecraft/client/Minecraft;",
                "Lave;",
                nullptr
            };
            
            for (int k = 0; signatures[k]; k++) {
                jfieldID field = env->GetStaticFieldID(clazz, instanceFieldNames[j], signatures[k]);
                if (field) {
                    env->ExceptionClear();
                    jobject instance = env->GetStaticObjectField(clazz, field);
                    if (instance) {
                        minecraftInstance = env->NewGlobalRef(instance);
                        env->DeleteLocalRef(instance);
                        OutputDebugStringA(("[WatermarkDLL] Found Minecraft instance via field: " + std::string(instanceFieldNames[j]) + "\n").c_str());
                        env->DeleteLocalRef(clazz);
                        return true;
                    }
                }
                env->ExceptionClear();
            }
        }
        
        // Try static method (correct signature: no args, returns MinecraftClient)
        const char* methodNames[] = {"getMinecraft", "getInstance", nullptr};
        for (int j = 0; methodNames[j]; j++) {
            std::string sig = "()L" + std::string(minecraftClassNames[i]) + ";";
            jmethodID method = env->GetStaticMethodID(clazz, methodNames[j], sig.c_str());
            if (method) {
                env->ExceptionClear();
                jobject instance = env->CallStaticObjectMethod(clazz, method);
                if (instance) {
                    minecraftInstance = env->NewGlobalRef(instance);
                    env->DeleteLocalRef(instance);
                    OutputDebugStringA(("[WatermarkDLL] Found Minecraft instance via method: " + std::string(methodNames[j]) + "\n").c_str());
                    env->DeleteLocalRef(clazz);
                    return true;
                }
            }
            env->ExceptionClear();
        }
        
        env->DeleteLocalRef(clazz);
    }
    
    OutputDebugStringA("[WatermarkDLL] Minecraft instance not found\n");
    return false;
}

bool JavaDetector::findCurrentScreenField() {
    if (!env || !minecraftInstance) return false;
    
    jclass clazz = env->GetObjectClass(minecraftInstance);
    if (!clazz) return false;
    
    // Try each field name with different type signatures
    const char* typeSigs[] = {
        "Lnet/minecraft/client/gui/GuiScreen;",
        "Lbft;",
        "Lbfh;",
        "Lnet/minecraft/client/gui/screen/Screen;",   // Yarn
        "Lnet/minecraft/class_437;",                  // Fabric intermediary Screen
        "Lnet/minecraft/client/gui/screens/Screen;",  // Mojmap 1.20.5+
        nullptr
    };
    
    for (int i = 0; currentScreenFieldNames[i]; i++) {
        for (int j = 0; typeSigs[j]; j++) {
            jfieldID field = env->GetFieldID(clazz, currentScreenFieldNames[i], typeSigs[j]);
            if (field) {
                env->ExceptionClear();
                currentScreenField = field;
                OutputDebugStringA(("[WatermarkDLL] Found currentScreen field: " + std::string(currentScreenFieldNames[i]) + "\n").c_str());
                env->DeleteLocalRef(clazz);
                return true;
            }
            env->ExceptionClear();
        }
    }
    
    env->DeleteLocalRef(clazz);
    OutputDebugStringA("[WatermarkDLL] currentScreen field not found\n");
    return false;
}

bool JavaDetector::findGuiChatClass() {
    if (!env) return false;
    
    for (int i = 0; guiChatClassNames[i]; i++) {
        jclass clazz = findClassEx(env, guiChatClassNames[i]);
        if (clazz) {
            guiChatClass = (jclass)env->NewGlobalRef(clazz);
            env->DeleteLocalRef(clazz);
            OutputDebugStringA(("[WatermarkDLL] Found GuiChat class: " + std::string(guiChatClassNames[i]) + "\n").c_str());
            return true;
        }
    }
    
    OutputDebugStringA("[WatermarkDLL] GuiChat class not found\n");
    return false;
}

bool JavaDetector::isGuiChatInstance(jobject screen) {
    if (!screen || !guiChatClass || !env) return false;
    
    return env->IsInstanceOf(screen, guiChatClass) != 0;
}

// ---- HUD-hidden (F1) detection ----
// options field names on the Minecraft class across mappings
static const char* optionsFieldNames[] = {
    "options",       // Yarn / Mojmap
    "gameSettings",  // MCP 1.8.9
    "field_1690",    // Fabric intermediary
    "field_71474_y", // 1.8.9 obfuscated
    nullptr
};
static const char* optionsTypeSigs[] = {
    "Lnet/minecraft/client/option/GameOptions;",    // Yarn
    "Lnet/minecraft/client/Options;",               // Mojmap
    "Lnet/minecraft/client/settings/GameSettings;", // MCP 1.8.9
    "Lnet/minecraft/class_315;",                    // Fabric intermediary GameOptions
    nullptr
};
static const char* hudHiddenFieldNames[] = {
    "hudHidden",   // Yarn / Mojmap
    "hideGUI",     // MCP 1.8.9
    "field_1842",  // Fabric intermediary
    nullptr
};

bool JavaDetector::resolveHudFields() {
    if (!env || !minecraftInstance) return false;

    jclass mcClass = env->GetObjectClass(minecraftInstance);
    if (!mcClass) return false;

    for (int i = 0; optionsFieldNames[i] && !optionsField; i++) {
        for (int j = 0; optionsTypeSigs[j] && !optionsField; j++) {
            jfieldID f = env->GetFieldID(mcClass, optionsFieldNames[i], optionsTypeSigs[j]);
            env->ExceptionClear();
            if (f) optionsField = f;
        }
    }
    if (!optionsField) { env->DeleteLocalRef(mcClass); return false; }

    jobject opts = env->GetObjectField(minecraftInstance, optionsField);
    env->ExceptionClear();
    if (!opts) { env->DeleteLocalRef(mcClass); return false; }

    jclass optClass = env->GetObjectClass(opts);
    if (optClass) {
        for (int i = 0; hudHiddenFieldNames[i] && !hudHiddenField; i++) {
            jfieldID f = env->GetFieldID(optClass, hudHiddenFieldNames[i], "Z");
            env->ExceptionClear();
            if (f) hudHiddenField = f;
        }
        env->DeleteLocalRef(optClass);
    }
    env->DeleteLocalRef(opts);
    env->DeleteLocalRef(mcClass);

    if (optionsField && hudHiddenField)
        OutputDebugStringA("[WatermarkDLL] HUD-hidden detection resolved (options.hudHidden)\n");
    return optionsField && hudHiddenField;
}

bool JavaDetector::isHudHidden() {
    if (!env || !minecraftInstance) return false;
    if (!hudHiddenField && !resolveHudFields()) return false;

    jobject opts = env->GetObjectField(minecraftInstance, optionsField);
    env->ExceptionClear();
    if (!opts) return false;
    bool hidden = env->GetBooleanField(opts, hudHiddenField) != 0;
    env->ExceptionClear();
    env->DeleteLocalRef(opts);
    return hidden;
}

// ---- In-game detection (world loaded?) ----
// world/level field names on the Minecraft class across mappings
static const char* levelFieldNames[] = {
    "world",          // Yarn
    "level",          // Mojmap
    "theWorld",       // MCP 1.8.9
    "field_1687",     // Fabric intermediary
    "field_71441_e",  // 1.8.9/1.12.2 obfuscated
    nullptr
};
static const char* levelTypeSigs[] = {
    "Lnet/minecraft/client/world/ClientWorld;",        // Yarn
    "Lnet/minecraft/client/multiplayer/ClientLevel;",  // Mojmap
    "Lnet/minecraft/client/multiplayer/WorldClient;",  // MCP 1.8.9
    "Lnet/minecraft/class_638;",                       // Fabric intermediary ClientWorld
    nullptr
};

bool JavaDetector::resolveLevelField() {
    if (!env || !minecraftInstance) return false;
    if (levelField) return true;

    jclass mcClass = env->GetObjectClass(minecraftInstance);
    if (!mcClass) return false;

    for (int i = 0; levelFieldNames[i] && !levelField; i++) {
        for (int j = 0; levelTypeSigs[j] && !levelField; j++) {
            jfieldID f = env->GetFieldID(mcClass, levelFieldNames[i], levelTypeSigs[j]);
            env->ExceptionClear();
            if (f) levelField = f;
        }
    }
    env->DeleteLocalRef(mcClass);

    if (levelField)
        OutputDebugStringA("[WatermarkDLL] In-game detection resolved (minecraft.world/level)\n");
    return levelField != nullptr;
}

bool JavaDetector::isInGame() {
    if (!env || !minecraftInstance) return false;
    if (!levelField && !resolveLevelField()) return false;

    jobject lvl = env->GetObjectField(minecraftInstance, levelField);
    env->ExceptionClear();
    if (!lvl) return false;
    env->DeleteLocalRef(lvl);
    return true;
}

bool JavaDetector::initialize() {
    if (jniInitialized) return true;
    
    OutputDebugStringA("[WatermarkDLL] Initializing JavaDetector...\n");
    
    // Step 1: Find JVM
    if (!findJVM()) {
        OutputDebugStringA("[WatermarkDLL] Failed to find JVM\n");
        return false;
    }
    
    // Step 2: Attach thread
    if (!attachThread()) {
        OutputDebugStringA("[WatermarkDLL] Failed to attach thread\n");
        cleanup();
        return false;
    }
    
    // Step 3: Find Minecraft instance (defer until needed)
    // We'll try to find it, but don't fail if not found yet
    if (!findMinecraftInstance()) {
        OutputDebugStringA("[WatermarkDLL] Minecraft instance not found yet, will retry later\n");
        // Don't fail, just return true - we'll retry finding instance later
    }
    
    // Step 4: Find currentScreen field (if we have instance)
    if (minecraftInstance) {
        findCurrentScreenField();
        findGuiChatClass();
    }
    
    jniInitialized = true;
    OutputDebugStringA("[WatermarkDLL] JavaDetector initialized\n");
    return true;
}

bool JavaDetector::isChatScreenOpen() {
    if (!jniInitialized || !env) return false;
    
    // Try to find Minecraft instance if not found yet
    if (!minecraftInstance) {
        findMinecraftInstance();
        if (minecraftInstance) {
            findCurrentScreenField();
            findGuiChatClass();
        }
        return false;
    }
    
    if (!currentScreenField) return false;
    
    // Get current screen
    jobject screen = env->GetObjectField(minecraftInstance, currentScreenField);
    if (!screen) {
        env->ExceptionClear();
        return false;
    }
    
    // Check if it's a GuiChat instance
    bool result = isGuiChatInstance(screen);
    env->DeleteLocalRef(screen);
    
    return result;
}

void JavaDetector::shutdown() {
    if (minecraftInstance && env) {
        env->DeleteGlobalRef(minecraftInstance);
        minecraftInstance = nullptr;
    }
    if (guiChatClass && env) {
        env->DeleteGlobalRef(guiChatClass);
        guiChatClass = nullptr;
    }
    cleanup();
    OutputDebugStringA("[WatermarkDLL] JavaDetector shutdown\n");
}
