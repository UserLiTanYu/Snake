package com.example.snake

import android.app.AlertDialog
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Bundle
import android.view.View
import androidx.activity.OnBackPressedCallback
import androidx.annotation.Keep
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    companion object {
        init {
            System.loadLibrary("snake")
        }
    }

    private external fun nativeIsAtMainMenu(): Boolean

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 核心：处理返回键事件
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (nativeIsAtMainMenu()) {
                    // 如果在主菜单或模式选择界面，弹出退出游戏确认框
                    showExitDialog()
                } else {
                    // 如果在游戏中，弹出返回主菜单确认框
                    showReturnToMenuDialog()
                }
            }
        })
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUi()
        }
    }

    private fun hideSystemUi() {
        val decorView = window.decorView
        decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN)
    }

    @Keep
    fun showExitDialog() {
        runOnUiThread {
            AlertDialog.Builder(this)
                .setTitle("退出游戏")
                .setMessage("确定要离开蛇蛇大作战吗？")
                .setPositiveButton("确认退出") { _, _ ->
                    finish()
                }
                .setNegativeButton("再玩一会", null)
                .show()
        }
    }

    // --- 新增：游戏中按下返回键的弹窗 ---
    @Keep
    fun showReturnToMenuDialog() {
        runOnUiThread {
            AlertDialog.Builder(this)
                .setTitle("是否要返回主菜单？")
                .setMessage("当前游戏进度将会丢失。")
                .setCancelable(false)
                .setPositiveButton("返回主菜单") { _, _ ->
                    nativeGoToMainMenu() // 调用 C++ 接口返回主菜单
                }
                .setNegativeButton("继续游戏", null) // 点击继续游戏则关闭弹窗，不做任何事
                .show()
        }
    }

    @Keep
    fun showGameOverDialog(score: Int) {
        runOnUiThread {
            AlertDialog.Builder(this)
                .setTitle("游戏结束")
                .setMessage("你的最终得分是: $score\n要再试一次吗？")
                .setCancelable(false)
                .setPositiveButton("重新开始") { _, _ ->
                    nativeRestartGame()
                }
                .setNegativeButton("返回主界面") { _, _ ->
                    nativeGoToMainMenu()
                }
                .show()
        }
    }

    @Keep
    fun getTextPixels(text: String, fontSize: Int): IntArray {
        val paint = Paint()
        paint.textSize = fontSize.toFloat()
        paint.color = Color.WHITE
        paint.textAlign = Paint.Align.CENTER
        paint.isAntiAlias = true
        paint.typeface = Typeface.DEFAULT_BOLD

        val width = paint.measureText(text).toInt().coerceAtLeast(1)
        val metrics = paint.fontMetrics
        val height = (metrics.bottom - metrics.top).toInt().coerceAtLeast(1)

        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawText(text, width / 2f, -metrics.top, paint)

        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        return intArrayOf(width, height) + pixels
    }

    @Keep
    fun getAssetPixels(fileName: String): IntArray {
        return try {
            val inputStream = assets.open(fileName)
            val bitmap = BitmapFactory.decodeStream(inputStream)
            val width = bitmap.width
            val height = bitmap.height
            val pixels = IntArray(width * height)
            bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
            bitmap.recycle()
            intArrayOf(width, height) + pixels
        } catch (e: Exception) {
            intArrayOf(0, 0)
        }
    }

    private external fun nativeRestartGame()
    private external fun nativeGoToMainMenu()
}