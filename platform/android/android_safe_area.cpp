/*!
 * @file android_safe_area.cpp
 * @brief `android_safe_area.h` の実装（JNI で `DisplayCutout` を辿る）。
 */
#include "android/android_safe_area.h"

#include <SDL.h>
#include <jni.h>

namespace platform_android {

namespace {

/*!
 * @brief 例外が出ていたら消して true を返す。
 * @details **JNI は例外を残したまま次の呼び出しへ進むと落ちる。**各段で必ず通すこと。
 * ここへ来るのは「窓がまだ画面に付いていない」等の想定内の失敗なので、記録もしない
 * （毎フレーム呼ぶので、出すと logcat が埋まる）。
 */
bool cleared_exception(JNIEnv *env)
{
    if (env->ExceptionCheck() == JNI_FALSE) {
        return false;
    }
    env->ExceptionClear();
    return true;
}

//! `obj` の `name()` を呼んで参照を返す（戻り値の綴りは `sig`）。失敗なら nullptr。
jobject call_object(JNIEnv *env, jobject obj, const char *name, const char *sig)
{
    if (obj == nullptr) {
        return nullptr;
    }
    jclass klass = env->GetObjectClass(obj);
    if (klass == nullptr) {
        (void)cleared_exception(env);
        return nullptr;
    }
    jmethodID method = env->GetMethodID(klass, name, sig);
    if ((method == nullptr) || cleared_exception(env)) {
        env->DeleteLocalRef(klass);
        (void)cleared_exception(env);
        return nullptr;
    }
    jobject result = env->CallObjectMethod(obj, method);
    env->DeleteLocalRef(klass);
    if (cleared_exception(env)) {
        return nullptr;
    }
    return result;
}

//! `obj` の `name()`（int を返す）を呼ぶ。失敗なら 0。
int call_int(JNIEnv *env, jobject obj, const char *name)
{
    jclass klass = env->GetObjectClass(obj);
    if (klass == nullptr) {
        (void)cleared_exception(env);
        return 0;
    }
    jmethodID method = env->GetMethodID(klass, name, "()I");
    if ((method == nullptr) || cleared_exception(env)) {
        env->DeleteLocalRef(klass);
        (void)cleared_exception(env);
        return 0;
    }
    const jint value = env->CallIntMethod(obj, method);
    env->DeleteLocalRef(klass);
    if (cleared_exception(env)) {
        return 0;
    }
    return static_cast<int>(value);
}

//! `obj` の `name(arg)`（int を 1 つ取り、綴り `sig` の参照を返す）を呼ぶ。失敗なら nullptr。
jobject call_object_int_arg(JNIEnv *env, jobject obj, const char *name, const char *sig, int arg)
{
    if (obj == nullptr) {
        return nullptr;
    }
    jclass klass = env->GetObjectClass(obj);
    if (klass == nullptr) {
        (void)cleared_exception(env);
        return nullptr;
    }
    jmethodID method = env->GetMethodID(klass, name, sig);
    if ((method == nullptr) || cleared_exception(env)) {
        env->DeleteLocalRef(klass);
        (void)cleared_exception(env);
        return nullptr;
    }
    jobject result = env->CallObjectMethod(obj, method, static_cast<jint>(arg));
    env->DeleteLocalRef(klass);
    if (cleared_exception(env)) {
        return nullptr;
    }
    return result;
}

/*!
 * @brief `obj` の int の**フィールド**を読む。失敗なら 0。
 * @details `android.graphics.Insets` は `left`/`top`/`right`/`bottom` を
 * **public final のフィールド**で持つ（getter が無い）ので、メソッドでは引けない。
 */
int read_int_field(JNIEnv *env, jobject obj, const char *name)
{
    if (obj == nullptr) {
        return 0;
    }
    jclass klass = env->GetObjectClass(obj);
    if (klass == nullptr) {
        (void)cleared_exception(env);
        return 0;
    }
    jfieldID field = env->GetFieldID(klass, name, "I");
    if ((field == nullptr) || cleared_exception(env)) {
        env->DeleteLocalRef(klass);
        (void)cleared_exception(env);
        return 0;
    }
    const jint value = env->GetIntField(obj, field);
    env->DeleteLocalRef(klass);
    if (cleared_exception(env)) {
        return 0;
    }
    return static_cast<int>(value);
}

//! `WindowInsets.Type.ime()`（静的メソッド）の値。引けなければ -1。
int window_insets_type_ime(JNIEnv *env)
{
    jclass klass = env->FindClass("android/view/WindowInsets$Type");
    if ((klass == nullptr) || cleared_exception(env)) {
        (void)cleared_exception(env);
        return -1;
    }
    jmethodID method = env->GetStaticMethodID(klass, "ime", "()I");
    if ((method == nullptr) || cleared_exception(env)) {
        env->DeleteLocalRef(klass);
        (void)cleared_exception(env);
        return -1;
    }
    const jint value = env->CallStaticIntMethod(klass, method);
    env->DeleteLocalRef(klass);
    if (cleared_exception(env)) {
        return -1;
    }
    return static_cast<int>(value);
}

} // namespace

bool get_safe_insets(int &left, int &top, int &right, int &bottom)
{
    auto *const env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (env == nullptr) {
        return false;
    }
    auto *const activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (activity == nullptr) {
        return false;
    }

    bool ok = false;
    jobject window = call_object(env, activity, "getWindow", "()Landroid/view/Window;");
    jobject decor = call_object(env, window, "getDecorView", "()Landroid/view/View;");
    /*
     * **`getRootWindowInsets()` は窓が画面に付くまで null を返す。**起動直後は普通に
     * 空振りするので、呼ぶ側が繰り返し試す前提で false を返す（失敗ではない）。
     */
    jobject insets = call_object(env, decor, "getRootWindowInsets", "()Landroid/view/WindowInsets;");
    jobject cutout = call_object(env, insets, "getDisplayCutout", "()Landroid/view/DisplayCutout;");
    if (insets != nullptr) {
        /*
         * **切り欠きが無い端末では `getDisplayCutout()` が null になる。**
         * それは「余白 0 が採れた」であって失敗ではない（ここで false を返すと、
         * 呼ぶ側が古い値を抱えたまま回転に追随できなくなる）。
         */
        ok = true;
        left = (cutout != nullptr) ? call_int(env, cutout, "getSafeInsetLeft") : 0;
        top = (cutout != nullptr) ? call_int(env, cutout, "getSafeInsetTop") : 0;
        right = (cutout != nullptr) ? call_int(env, cutout, "getSafeInsetRight") : 0;
        bottom = (cutout != nullptr) ? call_int(env, cutout, "getSafeInsetBottom") : 0;
    }

    if (cutout != nullptr) {
        env->DeleteLocalRef(cutout);
    }
    if (insets != nullptr) {
        env->DeleteLocalRef(insets);
    }
    if (decor != nullptr) {
        env->DeleteLocalRef(decor);
    }
    if (window != nullptr) {
        env->DeleteLocalRef(window);
    }
    env->DeleteLocalRef(activity); //!< `SDL_AndroidGetActivity` は局所参照を返す（**必ず捨てる**）
    return ok;
}

bool get_ime_inset(int &bottom)
{
    auto *const env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (env == nullptr) {
        return false;
    }
    auto *const activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (activity == nullptr) {
        return false;
    }

    bool ok = false;
    jobject window = call_object(env, activity, "getWindow", "()Landroid/view/Window;");
    jobject decor = call_object(env, window, "getDecorView", "()Landroid/view/View;");
    jobject insets = call_object(env, decor, "getRootWindowInsets", "()Landroid/view/WindowInsets;");
    if (insets != nullptr) {
        const int type = window_insets_type_ime(env);
        if (type >= 0) {
            jobject ime = call_object_int_arg(env, insets, "getInsets", "(I)Landroid/graphics/Insets;", type);
            if (ime != nullptr) {
                /*
                 * **キーボードが出ていないときは 0 が返る**（`getInsets` は null を返さない）。
                 * つまり「採れた・0 だった」と「採れなかった」は別物で、前者は
                 * 真を返さなければならない——偽で返すと、呼ぶ側が閉じたことに気づけず
                 * 割り付けが上がったまま戻らない。
                 */
                ok = true;
                bottom = read_int_field(env, ime, "bottom");
                env->DeleteLocalRef(ime);
            }
        }
    }

    if (insets != nullptr) {
        env->DeleteLocalRef(insets);
    }
    if (decor != nullptr) {
        env->DeleteLocalRef(decor);
    }
    if (window != nullptr) {
        env->DeleteLocalRef(window);
    }
    env->DeleteLocalRef(activity);
    return ok;
}

} // namespace platform_android
