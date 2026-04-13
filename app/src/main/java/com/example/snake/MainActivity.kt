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
    fun saveChallengeScore(mode: Int, score: Int) {
        val prefs = getSharedPreferences("GameData", MODE_PRIVATE)
        // mode对应C++中的枚举: CHALLENGE_1 = 1, CHALLENGE_2 = 2... 以此类推
        prefs.edit().putInt("ChallengeScore_$mode", score).apply()
    }

    // 读取对应关卡的最高分
    fun loadChallengeScore(mode: Int): Int {
        val prefs = getSharedPreferences("GameData", MODE_PRIVATE)
        return prefs.getInt("ChallengeScore_$mode", 0)
    }
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
        showCuteDialog(
            title = "退出游戏？", // 🥺
            message = "确定要离开贪吃蛇大作战吗！\n",
            posText = "再玩一会", // 绿色按钮留给积极选项
            negText = "确认退出", // 蓝色按钮留给消极选项
            onPos = { /* 玩家点击陪陪它，直接关闭弹窗即可 */ },
            onNeg = { finish() }
        )
    }


    @Keep
    fun showReturnToMenuDialog() {
        showCuteDialog(
            title = "是否要返回主菜单？", // 💨
            message = "当前游戏进度将会丢失！",
            posText = "继续游戏",
            negText = "返回主菜单",
            onPos = { /* 直接关闭弹窗，继续游戏 */ },
            onNeg = { nativeGoToMainMenu() }
        )
    }


    @Keep
    fun showGameOverDialog(score: Int, coins: Int) {
        showCuteDialog(
            title = "游戏结束！", // 💥
            message = "最终长度: $score\n收集金币: $coins\n\n要再试一次吗？", // 👑
            posText = "重新开始！",
            negText = "返回主界面",
            onPos = { nativeRestartGame() },
            onNeg = { nativeGoToMainMenu() }
        )
    }

    @Keep
    fun showChallengeClearDialog(stars: Int, score: Int) {
        // 根据传入的星星数量，生成星星字符串 (比如 3星就是 ⭐⭐⭐，2星就是 ⭐⭐☆)
        val starStr = "⭐".repeat(stars) + "☆".repeat(3 - stars)

        showCuteDialog(
            title = "挑战成功！", // 🎉
            message = "太棒了，顺利通关！\n\n获得评级: $starStr\n最终分数: $score",
            posText = "重新挑战",
            negText = "返回主界面",
            onPos = { nativeRestartGame() },
            onNeg = { nativeGoToMainMenu() }
        )
    }
    @Keep
    fun showTimeOutDialog(stars: Int, score: Int) {
        val starStr = "⭐".repeat(stars) + "☆".repeat(3 - stars)
        showCuteDialog(
            title = "时间到了！", // ⏰
            // --- 核心修改：在 message 中加入 本次评级: $starStr ---
            message = "哎呀，动作有点慢哦！\n\n本次评级: $starStr\n当前长度: $score\n\n差一点点就通关了，再抓紧点时间吧！",
            posText = "重新挑战",
            negText = "返回主界面",
            onPos = { nativeRestartGame() },
            onNeg = { nativeGoToMainMenu() }
        )
    }

    @Keep
    fun showChallengeFailDialog(stars: Int, score: Int) {
        // 根据获得的星星数生成图标，比如 1星就是 ⭐☆☆
        val starStr = "⭐".repeat(stars) + "☆".repeat(3 - stars)

        showCuteDialog(
            title = "挑战失败！", // 💔
            message = "哎呀，撞到了！\n\n本次评级: $starStr\n当前长度: $score\n\n不要灰心，再试一次吧！",
            posText = "重新挑战",
            negText = "返回主界面",
            onPos = { nativeRestartGame() },
            onNeg = { nativeGoToMainMenu() }
        )
    }
    @Keep
    fun showMazeClearDialog(stars: Int, timeUsed: Int) {
        val starStr = "⭐".repeat(stars) + "☆".repeat(3 - stars)
        showCuteDialog(
            title = "逃出迷宫！ \uD83C\uDF1F", // 🌟
            message = "太棒了，你成功找到了出口！\n\n获得评级: $starStr\n最终用时: ${timeUsed}秒",
            posText = "再次挑战",
            negText = "返回主界面",
            onPos = { nativeRestartGame() },
            onNeg = { nativeGoToMainMenu() }
        )
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

    // --- 核心新增：卡通可爱风弹窗构建器 ---
    private fun showCuteDialog(
        title: String,
        message: String,
        posText: String,
        negText: String,
        onPos: () -> Unit,
        onNeg: (() -> Unit)? = null
    ) {
        runOnUiThread {
            val dialog = android.app.Dialog(this)
            // 去除系统默认背景，让我们自定义的圆角生效
            dialog.window?.setBackgroundDrawable(android.graphics.drawable.ColorDrawable(Color.TRANSPARENT))
            dialog.setCancelable(false)

            val dp = { value: Int -> (value * resources.displayMetrics.density).toInt() }

            // 1. 弹窗主背景容器
            val layout = android.widget.LinearLayout(this).apply {
                orientation = android.widget.LinearLayout.VERTICAL
                setPadding(dp(24), dp(28), dp(24), dp(24))
                background = android.graphics.drawable.GradientDrawable().apply {
                    setColor(Color.parseColor("#FFFDF5")) // 奶黄色背景
                    cornerRadius = dp(24).toFloat()
                    setStroke(dp(4), Color.parseColor("#FFD166")) // 暖黄色卡通描边
                }
            }

            // 2. 弹窗标题
            val titleView = android.widget.TextView(this).apply {
                text = title
                textSize = 22f
                setTextColor(Color.parseColor("#FF6B6B")) // 活泼的粉红色
                typeface = Typeface.DEFAULT_BOLD
                gravity = android.view.Gravity.CENTER
                setPadding(0, 0, 0, dp(16))
            }
            layout.addView(titleView)

            // 3. 弹窗正文
            val msgView = android.widget.TextView(this).apply {
                text = message
                textSize = 16f
                setTextColor(Color.parseColor("#4A4E69")) // 深灰蓝，看起来很舒服
                gravity = android.view.Gravity.CENTER
                setPadding(0, 0, 0, dp(28))
            }
            layout.addView(msgView)

            // 4. 按钮排列容器
            val btnLayout = android.widget.LinearLayout(this).apply {
                orientation = android.widget.LinearLayout.HORIZONTAL
                gravity = android.view.Gravity.CENTER
            }

            // 左侧按钮 (通常是取消或退出)
            if (negText.isNotEmpty()) {
                val negBtn = android.widget.Button(this).apply {
                    text = negText
                    textSize = 16f
                    setTextColor(Color.WHITE)
                    typeface = Typeface.DEFAULT_BOLD
                    background = android.graphics.drawable.GradientDrawable().apply {
                        setColor(Color.parseColor("#8ECAE6")) // 浅蓝色
                        cornerRadius = dp(20).toFloat()
                    }
                    isAllCaps = false
                    setPadding(dp(16), dp(10), dp(16), dp(10))
                    setOnClickListener {
                        onNeg?.invoke()
                        dialog.dismiss()
                    }
                }
                val lp = android.widget.LinearLayout.LayoutParams(0, android.view.ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                lp.setMargins(0, 0, dp(8), 0)
                btnLayout.addView(negBtn, lp)
            }

            // 右侧按钮 (通常是继续或积极操作)
            val posBtn = android.widget.Button(this).apply {
                text = posText
                textSize = 16f
                setTextColor(Color.WHITE)
                typeface = Typeface.DEFAULT_BOLD
                background = android.graphics.drawable.GradientDrawable().apply {
                    setColor(Color.parseColor("#06D6A0")) // 活泼的薄荷绿
                    cornerRadius = dp(20).toFloat()
                }
                isAllCaps = false
                setPadding(dp(16), dp(10), dp(16), dp(10))
                setOnClickListener {
                    onPos.invoke()
                    dialog.dismiss()
                }
            }
            val posLp = android.widget.LinearLayout.LayoutParams(0, android.view.ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            if (negText.isNotEmpty()) posLp.setMargins(dp(8), 0, 0, 0)
            btnLayout.addView(posBtn, posLp)

            layout.addView(btnLayout)
            dialog.setContentView(layout)

            // 将弹窗宽度锁定为屏幕宽度的 80%，看起来刚刚好
            dialog.window?.setLayout((resources.displayMetrics.widthPixels * 0.80).toInt(), android.view.ViewGroup.LayoutParams.WRAP_CONTENT)
            dialog.show()
        }
    }



    private external fun nativeRestartGame()
    private external fun nativeGoToMainMenu()
}