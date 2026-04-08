package com.example.snake

import android.app.AlertDialog
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.SoundPool
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

    // --- 音频管理变量 ---
    private lateinit var soundPool: SoundPool
    private var eatSoundId = 0
    private var clinkSoundId = 0
    private var dieSoundId = 0

    private var menuBgmPlayer: MediaPlayer? = null
    private var gameBgmPlayer: MediaPlayer? = null
    private var currentMusicMode = 0 // 0=停止, 1=菜单BGM, 2=游戏BGM

    private external fun nativeIsAtMainMenu(): Boolean

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        initAudio() // 初始化音频

        // 处理返回键事件双重逻辑
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (nativeIsAtMainMenu()) {
                    showExitDialog() // 在主菜单：退出游戏
                } else {
                    showReturnToMenuDialog() // 在游戏中：返回主菜单
                }
            }
        })
    }

    // --- 极度健壮的音频初始化 ---
    private fun initAudio() {
        // 初始化 SoundPool 用于短促音效
        val audioAttributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        soundPool = SoundPool.Builder()
            .setMaxStreams(5)
            .setAudioAttributes(audioAttributes)
            .build()

        try {
            // 加载音效 (路径包含 musics/ 文件夹)
            eatSoundId = soundPool.load(assets.openFd("musics/eat.wav"), 1)
            clinkSoundId = soundPool.load(assets.openFd("musics/clink.wav"), 1)
            dieSoundId = soundPool.load(assets.openFd("musics/die.mp3"), 1)

            // 初始化 MediaPlayer 用于背景音乐，增加属性和错误监听
            val bgmAttributes = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build()

            menuBgmPlayer = MediaPlayer().apply {
                setAudioAttributes(bgmAttributes)
                val fd = assets.openFd("musics/bgm.mp3")
                setDataSource(fd.fileDescriptor, fd.startOffset, fd.length)
                fd.close() // 关键：关闭文件描述符
                isLooping = true
                setOnErrorListener { _, what, extra ->
                    android.util.Log.e("AudioDebug", "Menu BGM 发生错误: what=$what, extra=$extra")
                    true
                }
                prepare()
            }

            gameBgmPlayer = MediaPlayer().apply {
                setAudioAttributes(bgmAttributes)
                val fd = assets.openFd("musics/background.wav")
                setDataSource(fd.fileDescriptor, fd.startOffset, fd.length)
                fd.close() // 关键：关闭文件描述符
                isLooping = true
                setOnErrorListener { _, what, extra ->
                    android.util.Log.e("AudioDebug", "Game BGM 发生错误: what=$what, extra=$extra")
                    true
                }
                prepare()
            }
            android.util.Log.d("AudioDebug", "音频初始化全部成功！")
        } catch (e: Exception) {
            android.util.Log.e("AudioDebug", "音频初始化发生崩溃", e)
        }
    }

    // --- 供 C++ 调用的音效播放接口 ---
    @Keep
    fun playSoundEffect(soundType: Int) {
        val soundId = when (soundType) {
            1 -> eatSoundId
            2 -> clinkSoundId
            3 -> dieSoundId
            else -> return
        }
        soundPool.play(soundId, 1f, 1f, 1, 0, 1f)
    }

    // --- 带有日志追踪的 BGM 控制接口 (确保在主线程执行) ---
    @Keep
    fun playBackgroundMusic(musicMode: Int) {
        android.util.Log.d("AudioDebug", "C++ 请求切换音乐，目标模式: $musicMode")

        runOnUiThread {
            if (currentMusicMode == musicMode) return@runOnUiThread
            currentMusicMode = musicMode

            try {
                // 更安全的暂停逻辑
                if (menuBgmPlayer?.isPlaying == true) menuBgmPlayer?.pause()
                if (gameBgmPlayer?.isPlaying == true) gameBgmPlayer?.pause()

                when (musicMode) {
                    1 -> {
                        menuBgmPlayer?.seekTo(0)
                        menuBgmPlayer?.start()
                        android.util.Log.d("AudioDebug", "成功开始播放主界面 BGM")
                    }
                    2 -> {
                        gameBgmPlayer?.seekTo(0)
                        gameBgmPlayer?.start()
                        android.util.Log.d("AudioDebug", "成功开始播放游戏内 BGM")
                    }
                    0 -> {
                        android.util.Log.d("AudioDebug", "音乐已全部停止")
                    }
                }
            } catch (e: Exception) {
                android.util.Log.e("AudioDebug", "播放 BGM 时发生异常", e)
            }
        }
    }

    // 生命周期管理：进入后台时暂停音乐
    override fun onPause() {
        super.onPause()
        menuBgmPlayer?.pause()
        gameBgmPlayer?.pause()
    }

    override fun onResume() {
        super.onResume()
        if (currentMusicMode == 1) menuBgmPlayer?.start()
        if (currentMusicMode == 2) gameBgmPlayer?.start()
    }

    override fun onDestroy() {
        super.onDestroy()
        soundPool.release()
        menuBgmPlayer?.release()
        gameBgmPlayer?.release()
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

    @Keep
    fun showReturnToMenuDialog() {
        runOnUiThread {
            AlertDialog.Builder(this)
                .setTitle("是否要返回主菜单？")
                .setMessage("当前游戏进度将会丢失。")
                .setCancelable(false)
                .setPositiveButton("返回主菜单") { _, _ ->
                    nativeGoToMainMenu()
                }
                .setNegativeButton("继续游戏", null)
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