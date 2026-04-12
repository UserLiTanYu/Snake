package com.example.snake

import android.app.AlertDialog
import android.content.Context
import android.content.SharedPreferences
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
import android.text.InputFilter
import android.view.View
import android.widget.EditText
import androidx.activity.OnBackPressedCallback
import androidx.annotation.Keep
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    companion object {
        init {
            System.loadLibrary("snake")
        }
    }

    private lateinit var soundPool: SoundPool
    private var eatSoundId = 0
    private var clinkSoundId = 0
    private var dieSoundId = 0

    private var menuBgmPlayer: MediaPlayer? = null
    private var gameBgmPlayer: MediaPlayer? = null
    private var currentMusicMode = 0

    private var isMusicEnabled = true
    private var isSfxEnabled = true

    private lateinit var prefs: SharedPreferences

    private external fun nativeIsAtMainMenu(): Boolean

    private external fun nativeTryCloseMenuOverlay(): Boolean

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        prefs = getSharedPreferences("SnakeGamePrefs", Context.MODE_PRIVATE)
        initAudio()

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (nativeTryCloseMenuOverlay()) return
                if (nativeIsAtMainMenu()) {
                    showExitDialog()
                } else {
                    showReturnToMenuDialog()
                }
            }
        })
    }

    @Keep
    fun getCoins(): Int = prefs.getInt("coins", 0)

    @Keep
    fun addCoins(amount: Int) {
        prefs.edit().putInt("coins", getCoins() + amount).apply()
    }

    @Keep
    fun isSkinOwned(skinId: Int): Boolean {
        // --- 核心修改：0到7号为免费基础色皮肤，自动永久拥有 ---
        if (skinId <= 7) return true
        return prefs.getBoolean("skin_owned_$skinId", false)
    }

    @Keep
    fun buySkin(skinId: Int, price: Int): Boolean {
        val currentCoins = getCoins()
        if (currentCoins >= price && !isSkinOwned(skinId)) {
            prefs.edit()
                .putInt("coins", currentCoins - price)
                .putBoolean("skin_owned_$skinId", true)
                .apply()
            return true
        }
        return false
    }

    @Keep
    fun getEquippedSkin(): Int = prefs.getInt("equipped_skin", 0)

    @Keep
    fun equipSkin(skinId: Int) {
        if (isSkinOwned(skinId)) {
            prefs.edit().putInt("equipped_skin", skinId).apply()
        }
    }

    @Keep
    fun getPlayerDisplayName(): String {
        val raw = prefs.getString("player_display_name", null)?.trim().orEmpty()
        return if (raw.isEmpty()) "玩家" else raw.take(12)
    }

    @Keep
    fun setPlayerDisplayName(raw: String) {
        val t = raw.trim().take(12)
        prefs.edit().putString("player_display_name", if (t.isEmpty()) "玩家" else t).apply()
    }

    @Keep
    fun isShowSnakeHeadNames(): Boolean = prefs.getBoolean("show_snake_head_names", false)

    @Keep
    fun setShowSnakeHeadNames(enabled: Boolean) {
        prefs.edit().putBoolean("show_snake_head_names", enabled).apply()
    }

    @Keep
    fun showPlayerNameEditor() {
        runOnUiThread {
            val input = EditText(this).apply {
                setText(getPlayerDisplayName())
                filters = arrayOf(InputFilter.LengthFilter(12))
                setPadding(48, 32, 48, 32)
            }
            AlertDialog.Builder(this)
                .setTitle("修改昵称")
                .setView(input)
                .setPositiveButton("确定") { _, _ -> setPlayerDisplayName(input.text.toString()) }
                .setNegativeButton("取消", null)
                .show()
        }
    }

    private fun initAudio() {
        val audioAttributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()
        soundPool = SoundPool.Builder()
            .setMaxStreams(5)
            .setAudioAttributes(audioAttributes)
            .build()

        try {
            eatSoundId = soundPool.load(assets.openFd("musics/eat.wav"), 1)
            clinkSoundId = soundPool.load(assets.openFd("musics/clink.wav"), 1)
            dieSoundId = soundPool.load(assets.openFd("musics/die.mp3"), 1)

            val bgmAttributes = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build()

            menuBgmPlayer = MediaPlayer().apply {
                setAudioAttributes(bgmAttributes)
                val fd = assets.openFd("musics/bgm.mp3")
                setDataSource(fd.fileDescriptor, fd.startOffset, fd.length)
                fd.close()
                isLooping = true
                prepare()
            }

            gameBgmPlayer = MediaPlayer().apply {
                setAudioAttributes(bgmAttributes)
                val fd = assets.openFd("musics/background.wav")
                setDataSource(fd.fileDescriptor, fd.startOffset, fd.length)
                fd.close()
                isLooping = true
                prepare()
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    @Keep
    fun playSoundEffect(soundType: Int) {
        if (!isSfxEnabled) return
        val soundId = when (soundType) {
            1 -> eatSoundId
            2 -> clinkSoundId
            3 -> dieSoundId
            else -> return
        }
        soundPool.play(soundId, 1f, 1f, 1, 0, 1f)
    }

    @Keep
    fun playBackgroundMusic(musicMode: Int) {
        runOnUiThread {
            if (currentMusicMode == musicMode) return@runOnUiThread
            currentMusicMode = musicMode

            try {
                if (menuBgmPlayer?.isPlaying == true) menuBgmPlayer?.pause()
                if (gameBgmPlayer?.isPlaying == true) gameBgmPlayer?.pause()

                if (isMusicEnabled) {
                    when (musicMode) {
                        1 -> {
                            menuBgmPlayer?.seekTo(0)
                            menuBgmPlayer?.start()
                        }
                        2 -> {
                            gameBgmPlayer?.seekTo(0)
                            gameBgmPlayer?.start()
                        }
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    @Keep
    fun setAudioSetting(type: Int, enabled: Boolean) {
        runOnUiThread {
            if (type == 1) {
                isMusicEnabled = enabled
                if (!isMusicEnabled) {
                    menuBgmPlayer?.pause()
                    gameBgmPlayer?.pause()
                } else {
                    if (currentMusicMode == 1) menuBgmPlayer?.start()
                    if (currentMusicMode == 2) gameBgmPlayer?.start()
                }
            } else if (type == 2) {
                isSfxEnabled = enabled
            }
        }
    }

    override fun onPause() {
        super.onPause()
        menuBgmPlayer?.pause()
        gameBgmPlayer?.pause()
    }

    override fun onResume() {
        super.onResume()
        if (isMusicEnabled) {
            if (currentMusicMode == 1) menuBgmPlayer?.start()
            if (currentMusicMode == 2) gameBgmPlayer?.start()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        soundPool.release()
        menuBgmPlayer?.release()
        gameBgmPlayer?.release()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi()
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
                .setPositiveButton("确认退出") { _, _ -> finish() }
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
                .setPositiveButton("返回主菜单") { _, _ -> nativeGoToMainMenu() }
                .setNegativeButton("继续游戏", null)
                .show()
        }
    }

    @Keep
    fun showGameOverDialog(score: Int, coins: Int) {
        runOnUiThread {
            AlertDialog.Builder(this)
                .setTitle("游戏结束")
                .setMessage("你的最终得分是: $score\n获得金币: $coins\n要再试一次吗？")
                .setCancelable(false)
                .setPositiveButton("重新开始") { _, _ -> nativeRestartGame() }
                .setNegativeButton("返回主界面") { _, _ -> nativeGoToMainMenu() }
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
    fun getTextPixelsColored(text: String, fontSize: Int, colorArgb: Int): IntArray {
        val paint = Paint()
        paint.textSize = fontSize.toFloat()
        paint.color = colorArgb
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
        bitmap.recycle()
        return intArrayOf(width, height) + pixels
    }

    @Keep
    fun getTextPixelsLeft(text: String, fontSize: Int): IntArray {
        val paint = Paint()
        paint.textSize = fontSize.toFloat()
        paint.color = Color.WHITE
        paint.textAlign = Paint.Align.LEFT
        paint.isAntiAlias = true
        paint.typeface = Typeface.DEFAULT_BOLD

        val width = paint.measureText(text).toInt().coerceAtLeast(1)
        val metrics = paint.fontMetrics
        val height = (metrics.bottom - metrics.top).toInt().coerceAtLeast(1)

        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawText(text, 0f, -metrics.top, paint)

        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        bitmap.recycle()
        return intArrayOf(width, height) + pixels
    }

    @Keep
    fun getTextPixelsColoredLeft(text: String, fontSize: Int, colorArgb: Int): IntArray {
        val paint = Paint()
        paint.textSize = fontSize.toFloat()
        paint.color = colorArgb
        paint.textAlign = Paint.Align.LEFT
        paint.isAntiAlias = true
        paint.typeface = Typeface.DEFAULT_BOLD

        val width = paint.measureText(text).toInt().coerceAtLeast(1)
        val metrics = paint.fontMetrics
        val height = (metrics.bottom - metrics.top).toInt().coerceAtLeast(1)

        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawText(text, 0f, -metrics.top, paint)

        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        bitmap.recycle()
        return intArrayOf(width, height) + pixels
    }

    @Keep
    fun getTextPixelsRight(text: String, fontSize: Int): IntArray {
        val paint = Paint()
        paint.textSize = fontSize.toFloat()
        paint.color = Color.WHITE
        paint.textAlign = Paint.Align.RIGHT
        paint.isAntiAlias = true
        paint.typeface = Typeface.DEFAULT_BOLD

        val width = paint.measureText(text).toInt().coerceAtLeast(1)
        val metrics = paint.fontMetrics
        val height = (metrics.bottom - metrics.top).toInt().coerceAtLeast(1)

        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawText(text, width.toFloat(), -metrics.top, paint)

        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        bitmap.recycle()
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