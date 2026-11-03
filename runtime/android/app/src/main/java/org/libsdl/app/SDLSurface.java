package org.libsdl.app;

import android.content.Context;
import android.content.pm.ActivityInfo;
import android.graphics.Insets;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;

import android.view.ScaleGestureDetector;

public class SDLSurface extends SurfaceView implements SurfaceHolder.Callback,
    View.OnApplyWindowInsetsListener, View.OnKeyListener, View.OnTouchListener,
    SensorEventListener, ScaleGestureDetector.OnScaleGestureListener {

    protected SensorManager mSensorManager;
    protected Display mDisplay;

    protected float mWidth, mHeight;

    protected boolean mIsSurfaceReady;

    protected boolean mKeyboardVisible;

    private final ScaleGestureDetector scaleGestureDetector;

    protected SDLSurface(Context context) {
        super(context);
        getHolder().addCallback(this);

        scaleGestureDetector = new ScaleGestureDetector(context, this);

        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setOnApplyWindowInsetsListener(this);
        setOnKeyListener(this);
        setOnTouchListener(this);

        mDisplay = ((WindowManager)context.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
        mSensorManager = (SensorManager)context.getSystemService(Context.SENSOR_SERVICE);

        setOnGenericMotionListener(SDLActivity.getMotionListener());

        mWidth = 1.0f;
        mHeight = 1.0f;

        mIsSurfaceReady = false;
    }

    protected void handlePause() {
        enableSensor(Sensor.TYPE_ACCELEROMETER, false);
    }

    protected void handleResume() {
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setOnApplyWindowInsetsListener(this);
        setOnKeyListener(this);
        setOnTouchListener(this);
        enableSensor(Sensor.TYPE_ACCELEROMETER, true);
    }

    protected Surface getNativeSurface() {
        return getHolder().getSurface();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.v("SDL", "surfaceCreated()");
        SDLActivity.onNativeSurfaceCreated();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.v("SDL", "surfaceDestroyed()");

        SDLActivity.mNextNativeState = SDLActivity.NativeState.PAUSED;
        SDLActivity.handleNativeState();

        mIsSurfaceReady = false;
        SDLActivity.onNativeSurfaceDestroyed();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder,
                               int format, int width, int height) {
        Log.v("SDL", "surfaceChanged()");

        if (SDLActivity.mSingleton == null) {
            return;
        }

        mWidth = width;
        mHeight = height;
        int nDeviceWidth = width;
        int nDeviceHeight = height;
        float density = 1.0f;
        try
        {
            DisplayMetrics realMetrics = new DisplayMetrics();
            mDisplay.getRealMetrics( realMetrics );
            nDeviceWidth = realMetrics.widthPixels;
            nDeviceHeight = realMetrics.heightPixels;

            density = (float)realMetrics.densityDpi / 160.0f;
        } catch(Exception ignored) {
        }

        synchronized(SDLActivity.getContext()) {

            SDLActivity.getContext().notifyAll();
        }

        Log.v("SDL", "Window size: " + width + "x" + height);
        Log.v("SDL", "Device size: " + nDeviceWidth + "x" + nDeviceHeight);
        SDLActivity.nativeSetScreenResolution(width, height, nDeviceWidth, nDeviceHeight, density, mDisplay.getRefreshRate());
        SDLActivity.onNativeResize();

        boolean skip = false;
        int requestedOrientation = SDLActivity.mSingleton.getRequestedOrientation();

        if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT || requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT) {
            if (mWidth > mHeight) {
               skip = true;
            }
        } else if (requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE || requestedOrientation == ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE) {
            if (mWidth < mHeight) {
               skip = true;
            }
        }

        if (skip) {
           double min = Math.min(mWidth, mHeight);
           double max = Math.max(mWidth, mHeight);

           if (max / min < 1.20) {
              Log.v("SDL", "Don't skip on such aspect-ratio. Could be a square resolution.");
              skip = false;
           }
        }

        if (skip) {
            if (Build.VERSION.SDK_INT >= 24 ) {
                skip = false;
            }
        }

        if (skip) {
           Log.v("SDL", "Skip .. Surface is not ready.");
           mIsSurfaceReady = false;
           return;
        }

        SDLActivity.onNativeSurfaceChanged();

        mIsSurfaceReady = true;

        SDLActivity.mNextNativeState = SDLActivity.NativeState.RESUMED;
        SDLActivity.handleNativeState();
    }

    @Override
    public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30 ) {
            Insets combined = insets.getInsets(WindowInsets.Type.systemBars() |
                                               WindowInsets.Type.systemGestures() |
                                               WindowInsets.Type.mandatorySystemGestures() |
                                               WindowInsets.Type.tappableElement() |
                                               WindowInsets.Type.displayCutout());

            SDLActivity.onNativeInsetsChanged(combined.left, combined.right, combined.top, combined.bottom);

            if (insets.isVisible(WindowInsets.Type.ime())) {
                if (!mKeyboardVisible) {
                    mKeyboardVisible = true;
                    SDLActivity.onNativeScreenKeyboardShown();
                }
            } else {
                if (mKeyboardVisible) {
                    mKeyboardVisible = false;
                    SDLActivity.onNativeScreenKeyboardHidden();
                }
            }
        }

        return insets;
    }

    @Override
    public boolean onKey(View v, int keyCode, KeyEvent event) {
        return SDLActivity.handleKeyEvent(v, keyCode, event, null);
    }

    private float getNormalizedX(float x)
    {
        if (mWidth <= 1) {
            return 0.5f;
        } else {
            return (x / (mWidth - 1));
        }
    }

    private float getNormalizedY(float y)
    {
        if (mHeight <= 1) {
            return 0.5f;
        } else {
            return (y / (mHeight - 1));
        }
    }

    @Override
    public boolean onTouch(View v, MotionEvent event) {

        int touchDevId = event.getDeviceId();
        final int pointerCount = event.getPointerCount();
        int action = event.getActionMasked();
        int pointerId;
        int i = 0;
        float x,y,p;

        if (action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_POINTER_DOWN)
            i = event.getActionIndex();

        do {
            int toolType = event.getToolType(i);

            if (toolType == MotionEvent.TOOL_TYPE_MOUSE) {
                int buttonState = event.getButtonState();
                boolean relative = false;

                SDLGenericMotionListener_API14 motionListener = SDLActivity.getMotionListener();
                x = motionListener.getEventX(event, i);
                y = motionListener.getEventY(event, i);
                relative = motionListener.inRelativeMode();

                SDLActivity.onNativeMouse(buttonState, action, x, y, relative);
            } else if (toolType == MotionEvent.TOOL_TYPE_STYLUS || toolType == MotionEvent.TOOL_TYPE_ERASER) {
                pointerId = event.getPointerId(i);
                x = event.getX(i);
                y = event.getY(i);
                p = event.getPressure(i);
                if (p > 1.0f) {

                    p = 1.0f;
                }

                int buttonState = (event.getButtonState() >> 4) | (1 << (toolType == MotionEvent.TOOL_TYPE_STYLUS ? 0 : 30));
                if ((event.getButtonState() & MotionEvent.BUTTON_TERTIARY) != 0) {
                    buttonState |= 0x08;
                }

                SDLActivity.onNativePen(pointerId, SDLActivity.getMotionListener().getPenDeviceType(event.getDevice()), buttonState, action, x, y, p);
            } else {
                pointerId = event.getPointerId(i);
                x = getNormalizedX(event.getX(i));
                y = getNormalizedY(event.getY(i));
                p = event.getPressure(i);
                if (p > 1.0f) {

                    p = 1.0f;
                }

                SDLActivity.onNativeTouch(touchDevId, pointerId, action, x, y, p);
            }

            if (action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_POINTER_DOWN)
                break;
        } while (++i < pointerCount);

        scaleGestureDetector.onTouchEvent(event);

        return true;
    }

    protected void enableSensor(int sensortype, boolean enabled) {

        if (enabled) {
            SDLSensorManager.registerListener(mSensorManager, this,
                            mSensorManager.getDefaultSensor(sensortype),
                            SensorManager.SENSOR_DELAY_GAME);
        } else {
            SDLSensorManager.unregisterListener(mSensorManager, this,
                            mSensorManager.getDefaultSensor(sensortype));
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {

    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {

            int newRotation;

            float x, y;
            switch (mDisplay.getRotation()) {
                case Surface.ROTATION_0:
                default:
                    x = event.values[0];
                    y = event.values[1];
                    newRotation = 0;
                    break;
                case Surface.ROTATION_90:
                    x = -event.values[1];
                    y = event.values[0];
                    newRotation = 90;
                    break;
                case Surface.ROTATION_180:
                    x = -event.values[0];
                    y = -event.values[1];
                    newRotation = 180;
                    break;
                case Surface.ROTATION_270:
                    x = event.values[1];
                    y = -event.values[0];
                    newRotation = 270;
                    break;
            }

            if (newRotation != SDLActivity.mCurrentRotation) {
                SDLActivity.mCurrentRotation = newRotation;
                SDLActivity.onNativeRotationChanged(newRotation);
            }

            SDLActivity.onNativeAccel(-x / SensorManager.GRAVITY_EARTH,
                                      y / SensorManager.GRAVITY_EARTH,
                                      event.values[2] / SensorManager.GRAVITY_EARTH);

        }
    }

    @Override
    public PointerIcon onResolvePointerIcon(MotionEvent event, int pointerIndex) {
        try {
            return super.onResolvePointerIcon(event, pointerIndex);
        } catch (NullPointerException e) {
            return null;
        }
    }

    @Override
    public boolean onCapturedPointerEvent(MotionEvent event)
    {
        return SDLActivity.getMotionListener().onGenericMotion(this, event);
    }

    @Override
    public boolean onScale(ScaleGestureDetector detector) {
        float scale = detector.getScaleFactor();
        SDLActivity.onNativePinchUpdate(scale);
        return true;
    }

    @Override
    public boolean onScaleBegin(ScaleGestureDetector detector) {
        SDLActivity.onNativePinchStart();
        return true;
    }

    @Override
    public void onScaleEnd(ScaleGestureDetector detector) {
        SDLActivity.onNativePinchEnd();
    }

}
