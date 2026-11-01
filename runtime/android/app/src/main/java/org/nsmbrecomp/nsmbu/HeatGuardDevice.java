package org.nsmbrecomp.nsmbu;

import java.util.Locale;

/**
 * The kind of device for the heat guard (runtime/src/platform/heat_guard.h): actively cooled
 * Android handhelds pause the in-between frames later (thermal headroom 1.00, resume below 0.90) than phones and
 * unknown devices (0.72 / 0.60), whose limits were tuned on fanless phones. Matching is pure, so
 * tools/android/HeatGuardDeviceTest.java covers it; the result reaches native code through
 * nativeSetDeviceClass before the game starts.
 */
final class HeatGuardDevice {
    private HeatGuardDevice() {}

    /** The values of heat_guard::DeviceClass. */
    static final int PHONE = 0;
    static final int FAN_HANDHELD = 1;

    private static native void nativeSetDeviceClass(int deviceClass);

    /** Build.MANUFACTURER, Build.BRAND and Build.MODEL (any may be null). */
    static int classify(String manufacturer, String brand, String model) {
        return reason(manufacturer, brand, model) != null ? FAN_HANDHELD : PHONE;
    }

    /** Why the device counts as a fan-cooled handheld, or null for phones and unknown devices. */
    static String reason(String manufacturer, String brand, String model) {
        String maker = norm(manufacturer), make = norm(brand), name = norm(model);
        if (either(maker, make, "ayaneo") || either(maker, make, "aya") || name.startsWith("ayaneo"))
            return "AYANEO handheld (fan)";
        if (either(maker, make, "konkr") || name.startsWith("konkr"))
            return "KONKR (AYANEO) handheld (fan)";
        if (either(maker, make, "ayn") || name.startsWith("odin"))
            return "AYN handheld (fan)";
        if (maker.contains("retroid") || make.contains("retroid") || name.startsWith("retroid"))
            return "Retroid handheld (fan)";
        return null;
    }

    private static String norm(String s) { return s == null ? "" : s.trim().toLowerCase(Locale.ROOT); }
    private static boolean either(String a, String b, String want) { return a.equals(want) || b.equals(want); }

    /** Tells native code which kind of device this is (Build.MANUFACTURER, BRAND, MODEL); returns the class. */
    static int publish(String manufacturer, String brand, String model) {
        int deviceClass = classify(manufacturer, brand, model);
        nativeSetDeviceClass(deviceClass);
        return deviceClass;
    }
}
