package com.example.snake

import android.app.AlertDialog
import android.os.Bundle
import android.view.View
import androidx.annotation.Keep
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    companion object {
        init {
            System.loadLibrary("snake")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
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

    // 使用 @Keep 确保方法不被混淆
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
                .setNegativeButton("退出") { _, _ ->
                    finish()
                }
                .show()
        }
    }

    private external fun nativeRestartGame()
}