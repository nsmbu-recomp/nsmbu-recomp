package org.libsdl.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.os.Build;
import android.util.Log;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.hardware.usb.*;
import android.os.Handler;
import android.os.Looper;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;

public class HIDDeviceManager {
    private static final String TAG = "hidapi";
    private static final String ACTION_USB_PERMISSION = "org.libsdl.app.USB_PERMISSION";

    private static HIDDeviceManager sManager;
    private static int sManagerRefCount = 0;

    static public HIDDeviceManager acquire(Context context) {
        if (sManagerRefCount == 0) {
            sManager = new HIDDeviceManager(context);
        }
        ++sManagerRefCount;
        return sManager;
    }

    static public void release(HIDDeviceManager manager) {
        if (manager == sManager) {
            --sManagerRefCount;
            if (sManagerRefCount == 0) {
                sManager.close();
                sManager = null;
            }
        }
    }

    private Context mContext;
    private HashMap<Integer, HIDDevice> mDevicesById = new HashMap<Integer, HIDDevice>();
    private HashMap<BluetoothDevice, HIDDeviceBLESteamController> mBluetoothDevices = new HashMap<BluetoothDevice, HIDDeviceBLESteamController>();
    private int mNextDeviceId = 0;
    private SharedPreferences mSharedPreferences = null;
    private boolean mIsChromebook = false;
    private UsbManager mUsbManager;
    private Handler mHandler;
    private BluetoothManager mBluetoothManager;
    private List<BluetoothDevice> mLastBluetoothDevices;

    private final BroadcastReceiver mUsbBroadcast = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (action.equals(UsbManager.ACTION_USB_DEVICE_ATTACHED)) {
                UsbDevice usbDevice = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                handleUsbDeviceAttached(usbDevice);
            } else if (action.equals(UsbManager.ACTION_USB_DEVICE_DETACHED)) {
                UsbDevice usbDevice = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                handleUsbDeviceDetached(usbDevice);
            } else if (action.equals(HIDDeviceManager.ACTION_USB_PERMISSION)) {
                UsbDevice usbDevice = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                handleUsbDevicePermission(usbDevice, intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false));
            }
        }
    };

    private final BroadcastReceiver mBluetoothBroadcast = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();

            if (action.equals(BluetoothDevice.ACTION_ACL_CONNECTED)) {
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                Log.d(TAG, "Bluetooth device connected: " + device);

                if (isSteamController(device)) {
                    connectBluetoothDevice(device);
                }
            }

            if (action.equals(BluetoothDevice.ACTION_ACL_DISCONNECTED)) {
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                Log.d(TAG, "Bluetooth device disconnected: " + device);

                disconnectBluetoothDevice(device);
            }
        }
    };

    private HIDDeviceManager(final Context context) {
        mContext = context;

        HIDDeviceRegisterCallback();

        mSharedPreferences = mContext.getSharedPreferences("hidapi", Context.MODE_PRIVATE);
        mIsChromebook = SDLActivity.isChromebook();

        {
            mNextDeviceId = mSharedPreferences.getInt("next_device_id", 0);
        }
    }

    Context getContext() {
        return mContext;
    }

    int getDeviceIDForIdentifier(String identifier) {
        SharedPreferences.Editor spedit = mSharedPreferences.edit();

        int result = mSharedPreferences.getInt(identifier, 0);
        if (result == 0) {
            result = mNextDeviceId++;
            spedit.putInt("next_device_id", mNextDeviceId);
        }

        spedit.putInt(identifier, result);
        spedit.apply();
        return result;
    }

    private void initializeUSB() {
        mUsbManager = (UsbManager)mContext.getSystemService(Context.USB_SERVICE);
        if (mUsbManager == null) {
            return;
        }

        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        filter.addAction(HIDDeviceManager.ACTION_USB_PERMISSION);
        if (Build.VERSION.SDK_INT >= 33) {
            mContext.registerReceiver(mUsbBroadcast, filter, Context.RECEIVER_EXPORTED);
        } else {
            mContext.registerReceiver(mUsbBroadcast, filter);
        }

        for (UsbDevice usbDevice : mUsbManager.getDeviceList().values()) {
            handleUsbDeviceAttached(usbDevice);
        }
    }

    UsbManager getUSBManager() {
        return mUsbManager;
    }

    private void shutdownUSB() {
        try {
            mContext.unregisterReceiver(mUsbBroadcast);
        } catch (Exception e) {

        }
    }

    private boolean isHIDDeviceInterface(UsbDevice usbDevice, UsbInterface usbInterface) {
        if (usbInterface.getInterfaceClass() == UsbConstants.USB_CLASS_HID) {
            return true;
        }
        if (isXbox360Controller(usbDevice, usbInterface) || isXboxOneController(usbDevice, usbInterface)) {
            return true;
        }
        return false;
    }

    private boolean isXbox360Controller(UsbDevice usbDevice, UsbInterface usbInterface) {
        final int XB360_IFACE_SUBCLASS = 93;
        final int XB360_IFACE_PROTOCOL = 1;
        final int XB360W_IFACE_PROTOCOL = 129;
        final int[] SUPPORTED_VENDORS = {
            0x0079,
            0x0351,
            0x044f,
            0x045e,
            0x046d,
            0x056e,
            0x06a3,
            0x0738,
            0x07ff,
            0x0e6f,
            0x0f0d,
            0x1038,
            0x10f5,
            0x11c9,
            0x1209,
            0x12ab,
            0x1430,
            0x146b,
            0x1532,
            0x15e4,
            0x162e,
            0x1689,
            0x1949,
            0x1bad,
            0x20d6,
            0x24c6,
            0x2c22,
            0x2dc8,
            0x3537,
            0x3651,
            0x37d7,
            0x3958,
            0x9886,
        };

        if (usbInterface.getInterfaceClass() == UsbConstants.USB_CLASS_VENDOR_SPEC &&
            usbInterface.getInterfaceSubclass() == XB360_IFACE_SUBCLASS &&
            (usbInterface.getInterfaceProtocol() == XB360_IFACE_PROTOCOL ||
             usbInterface.getInterfaceProtocol() == XB360W_IFACE_PROTOCOL)) {
            int vendor_id = usbDevice.getVendorId();
            for (int supportedVid : SUPPORTED_VENDORS) {
                if (vendor_id == supportedVid) {
                    return true;
                }
            }
        }
        return false;
    }

    private boolean isXboxOneController(UsbDevice usbDevice, UsbInterface usbInterface) {
        final int XB1_IFACE_SUBCLASS = 71;
        final int XB1_IFACE_PROTOCOL = 208;
        final int[] SUPPORTED_VENDORS = {
            0x0351,
            0x03f0,
            0x044f,
            0x045e,
            0x0738,
            0x0b05,
            0x0e6f,
            0x0f0d,
            0x10f5,
            0x1209,
            0x1532,
            0x20d6,
            0x24c6,
            0x294b,
            0x2dc8,
            0x2e24,
            0x2e95,
            0x3285,
            0x3537,
            0x3651,
            0x366c,
            0x3958,
        };

        if (usbInterface.getId() == 0 &&
            usbInterface.getInterfaceClass() == UsbConstants.USB_CLASS_VENDOR_SPEC &&
            usbInterface.getInterfaceSubclass() == XB1_IFACE_SUBCLASS &&
            usbInterface.getInterfaceProtocol() == XB1_IFACE_PROTOCOL) {
            int vendor_id = usbDevice.getVendorId();
            for (int supportedVid : SUPPORTED_VENDORS) {
                if (vendor_id == supportedVid) {
                    return true;
                }
            }
        }
        return false;
    }

    private void handleUsbDeviceAttached(UsbDevice usbDevice) {
        connectHIDDeviceUSB(usbDevice);
    }

    private void handleUsbDeviceDetached(UsbDevice usbDevice) {
        List<Integer> devices = new ArrayList<Integer>();
        for (HIDDevice device : mDevicesById.values()) {
            if (usbDevice.equals(device.getDevice())) {
                devices.add(device.getId());
            }
        }
        for (int id : devices) {
            HIDDevice device = mDevicesById.get(id);
            mDevicesById.remove(id);
            device.shutdown();
            HIDDeviceDisconnected(id);
        }
    }

    private void handleUsbDevicePermission(UsbDevice usbDevice, boolean permission_granted) {
        for (HIDDevice device : mDevicesById.values()) {
            if (usbDevice.equals(device.getDevice())) {
                boolean opened = false;
                if (permission_granted) {
                    opened = device.open();
                }
                HIDDeviceOpenResult(device.getId(), opened);
            }
        }
    }

    private void connectHIDDeviceUSB(UsbDevice usbDevice) {
        synchronized (this) {
            int interface_mask = 0;
            for (int interface_index = 0; interface_index < usbDevice.getInterfaceCount(); interface_index++) {
                UsbInterface usbInterface = usbDevice.getInterface(interface_index);
                if (isHIDDeviceInterface(usbDevice, usbInterface)) {

                    int interface_id = usbInterface.getId();
                    if ((interface_mask & (1 << interface_id)) != 0) {
                        continue;
                    }
                    interface_mask |= (1 << interface_id);

                    HIDDeviceUSB device = new HIDDeviceUSB(this, usbDevice, interface_index);
                    int id = device.getId();
                    mDevicesById.put(id, device);
                    HIDDeviceConnected(id, device.getIdentifier(), device.getVendorId(), device.getProductId(), device.getSerialNumber(), device.getVersion(), device.getManufacturerName(), device.getProductName(), usbInterface.getId(), usbInterface.getInterfaceClass(), usbInterface.getInterfaceSubclass(), usbInterface.getInterfaceProtocol(), false, 0);
                }
            }
        }
    }

    private void initializeBluetooth() {
        Log.d(TAG, "Initializing Bluetooth");

        if (Build.VERSION.SDK_INT >= 31  &&
            mContext.getPackageManager().checkPermission(android.Manifest.permission.BLUETOOTH_CONNECT, mContext.getPackageName()) != PackageManager.PERMISSION_GRANTED) {
            Log.d(TAG, "Couldn't initialize Bluetooth, missing android.permission.BLUETOOTH_CONNECT");
            return;
        }

        if (Build.VERSION.SDK_INT <= 30  &&
            mContext.getPackageManager().checkPermission(android.Manifest.permission.BLUETOOTH, mContext.getPackageName()) != PackageManager.PERMISSION_GRANTED) {
            Log.d(TAG, "Couldn't initialize Bluetooth, missing android.permission.BLUETOOTH");
            return;
        }

        if (!mContext.getPackageManager().hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            Log.d(TAG, "Couldn't initialize Bluetooth, this version of Android does not support Bluetooth LE");
            return;
        }

        mBluetoothManager = (BluetoothManager)mContext.getSystemService(Context.BLUETOOTH_SERVICE);
        if (mBluetoothManager == null) {

            return;
        }

        BluetoothAdapter btAdapter = mBluetoothManager.getAdapter();
        if (btAdapter == null) {

            return;
        }

        for (BluetoothDevice device : btAdapter.getBondedDevices()) {

            Log.d(TAG, "Bluetooth device available: " + device);
            if (isSteamController(device)) {
                connectBluetoothDevice(device);
            }

        }

        IntentFilter filter = new IntentFilter();
        filter.addAction(BluetoothDevice.ACTION_ACL_CONNECTED);
        filter.addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED);
        if (Build.VERSION.SDK_INT >= 33) {
            mContext.registerReceiver(mBluetoothBroadcast, filter, Context.RECEIVER_EXPORTED);
        } else {
            mContext.registerReceiver(mBluetoothBroadcast, filter);
        }

        if (mIsChromebook) {
            mHandler = new Handler(Looper.getMainLooper());
            mLastBluetoothDevices = new ArrayList<BluetoothDevice>();

        }
    }

    private void shutdownBluetooth() {
        try {
            mContext.unregisterReceiver(mBluetoothBroadcast);
        } catch (Exception e) {

        }
    }

    void chromebookConnectionHandler() {
        if (!mIsChromebook) {
            return;
        }

        ArrayList<BluetoothDevice> disconnected = new ArrayList<BluetoothDevice>();
        ArrayList<BluetoothDevice> connected = new ArrayList<BluetoothDevice>();

        List<BluetoothDevice> currentConnected = mBluetoothManager.getConnectedDevices(BluetoothProfile.GATT);

        for (BluetoothDevice bluetoothDevice : currentConnected) {
            if (!mLastBluetoothDevices.contains(bluetoothDevice)) {
                connected.add(bluetoothDevice);
            }
        }
        for (BluetoothDevice bluetoothDevice : mLastBluetoothDevices) {
            if (!currentConnected.contains(bluetoothDevice)) {
                disconnected.add(bluetoothDevice);
            }
        }

        mLastBluetoothDevices = currentConnected;

        for (BluetoothDevice bluetoothDevice : disconnected) {
            disconnectBluetoothDevice(bluetoothDevice);
        }
        for (BluetoothDevice bluetoothDevice : connected) {
            connectBluetoothDevice(bluetoothDevice);
        }

        final HIDDeviceManager finalThis = this;
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                finalThis.chromebookConnectionHandler();
            }
        }, 10000);
    }

    boolean connectBluetoothDevice(BluetoothDevice bluetoothDevice) {
        Log.v(TAG, "connectBluetoothDevice device=" + bluetoothDevice);
        synchronized (this) {
            if (mBluetoothDevices.containsKey(bluetoothDevice)) {
                Log.v(TAG, "Steam controller with address " + bluetoothDevice + " already exists, attempting reconnect");

                HIDDeviceBLESteamController device = mBluetoothDevices.get(bluetoothDevice);
                device.reconnect();

                return false;
            }
            HIDDeviceBLESteamController device = new HIDDeviceBLESteamController(this, bluetoothDevice);
            int id = device.getId();
            mBluetoothDevices.put(bluetoothDevice, device);
            mDevicesById.put(id, device);

        }
        return true;
    }

    void disconnectBluetoothDevice(BluetoothDevice bluetoothDevice) {
        synchronized (this) {
            HIDDeviceBLESteamController device = mBluetoothDevices.get(bluetoothDevice);
            if (device == null)
                return;

            int id = device.getId();
            mBluetoothDevices.remove(bluetoothDevice);
            mDevicesById.remove(id);
            device.shutdown();
            HIDDeviceDisconnected(id);
        }
    }

    boolean isSteamController(BluetoothDevice bluetoothDevice) {

        if (bluetoothDevice == null) {
            return false;
        }

        if (bluetoothDevice.getName() == null) {
            return false;
        }

        if ((bluetoothDevice.getType() & BluetoothDevice.DEVICE_TYPE_LE) == 0) {
            return false;
        }

        return bluetoothDevice.getName().equals("SteamController") || bluetoothDevice.getName().startsWith("Steam Ctrl");
    }

    private void close() {
        shutdownUSB();
        shutdownBluetooth();
        synchronized (this) {
            for (HIDDevice device : mDevicesById.values()) {
                device.shutdown();
            }
            mDevicesById.clear();
            mBluetoothDevices.clear();
            HIDDeviceReleaseCallback();
        }
    }

    public void setFrozen(boolean frozen) {
        synchronized (this) {
            for (HIDDevice device : mDevicesById.values()) {
                device.setFrozen(frozen);
            }
        }
    }

    private HIDDevice getDevice(int id) {
        synchronized (this) {
            HIDDevice result = mDevicesById.get(id);
            if (result == null) {
                Log.v(TAG, "No device for id: " + id);
                Log.v(TAG, "Available devices: " + mDevicesById.keySet());
            }
            return result;
        }
    }

    boolean initialize(boolean usb, boolean bluetooth) {
        Log.v(TAG, "initialize(" + usb + ", " + bluetooth + ")");

        if (usb) {
            initializeUSB();
        }
        if (bluetooth) {
            initializeBluetooth();
        }
        return true;
    }

    boolean openDevice(int deviceID) {
        Log.v(TAG, "openDevice deviceID=" + deviceID);
        HIDDevice device = getDevice(deviceID);
        if (device == null) {
            HIDDeviceDisconnected(deviceID);
            return false;
        }

        UsbDevice usbDevice = device.getDevice();
        if (usbDevice != null && !mUsbManager.hasPermission(usbDevice)) {
            HIDDeviceOpenPending(deviceID);
            try {
                final int FLAG_MUTABLE = 0x02000000;
                int flags;
                if (Build.VERSION.SDK_INT >= 31 ) {
                    flags = FLAG_MUTABLE;
                } else {
                    flags = 0;
                }

                Intent intent = new Intent(HIDDeviceManager.ACTION_USB_PERMISSION);
                intent.setPackage(mContext.getPackageName());
                mUsbManager.requestPermission(usbDevice, PendingIntent.getBroadcast(mContext, 0, intent, flags));
            } catch (Exception e) {
                Log.v(TAG, "Couldn't request permission for USB device " + usbDevice);
                HIDDeviceOpenResult(deviceID, false);
            }
            return false;
        }

        try {
            return device.open();
        } catch (Exception e) {
            Log.e(TAG, "Got exception: " + Log.getStackTraceString(e));
        }
        return false;
    }

    int writeReport(int deviceID, byte[] report, boolean feature) {
        try {

            HIDDevice device;
            device = getDevice(deviceID);
            if (device == null) {
                HIDDeviceDisconnected(deviceID);
                return -1;
            }

            return device.writeReport(report, feature);
        } catch (Exception e) {
            Log.e(TAG, "Got exception: " + Log.getStackTraceString(e));
        }
        return -1;
    }

    boolean readReport(int deviceID, byte[] report, boolean feature) {
        try {

            HIDDevice device;
            device = getDevice(deviceID);
            if (device == null) {
                HIDDeviceDisconnected(deviceID);
                return false;
            }

            return device.readReport(report, feature);
        } catch (Exception e) {
            Log.e(TAG, "Got exception: " + Log.getStackTraceString(e));
        }
        return false;
    }

    void closeDevice(int deviceID) {
        try {
            Log.v(TAG, "closeDevice deviceID=" + deviceID);
            HIDDevice device;
            device = getDevice(deviceID);
            if (device == null) {
                HIDDeviceDisconnected(deviceID);
                return;
            }

            device.close();
        } catch (Exception e) {
            Log.e(TAG, "Got exception: " + Log.getStackTraceString(e));
        }
    }

    private native void HIDDeviceRegisterCallback();
    private native void HIDDeviceReleaseCallback();

    native void HIDDeviceConnected(int deviceID, String identifier, int vendorId, int productId, String serial_number, int release_number, String manufacturer_string, String product_string, int interface_number, int interface_class, int interface_subclass, int interface_protocol, boolean bBluetooth, int reportID);
    native void HIDDeviceOpenPending(int deviceID);
    native void HIDDeviceOpenResult(int deviceID, boolean opened);
    native void HIDDeviceDisconnected(int deviceID);

    native void HIDDeviceInputReport(int deviceID, byte[] report);
    native void HIDDeviceReportResponse(int deviceID, byte[] report);
}
