/**
 * MainActivity.kt
 * Initial activity of the demo app
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */
package nz.mega.android.bindingsample

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.view.View.OnClickListener
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import nz.mega.sdk.MegaApiAndroid
import nz.mega.sdk.MegaApiJava
import nz.mega.sdk.MegaError
import nz.mega.sdk.MegaRequest
import nz.mega.sdk.MegaRequestListenerInterface
import java.util.Locale

class MainActivity : Activity(), OnClickListener, MegaRequestListenerInterface {

    private lateinit var title: TextView
    private lateinit var loginText: EditText
    private lateinit var passwordText: EditText
    private lateinit var loginButton: Button
    private lateinit var fetchingNodesBar: ProgressBar

    private var email = ""
    private var password = ""

    private var gPublicKey = ""
    private var gPrivateKey = ""

    private lateinit var megaApi: MegaApiAndroid

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val app = application as DemoAndroidApplication
        megaApi = app.getMegaApi()

        setContentView(R.layout.activity_main)

        title = findViewById(R.id.title_text_view)
        loginText = findViewById(R.id.email_text)
        passwordText = findViewById(R.id.password_text)
        loginButton = findViewById(R.id.button_login)
        loginButton.setOnClickListener(this)
        fetchingNodesBar = findViewById(R.id.fetching_nodes_bar)

        title.text = resources.getString(R.string.login_text)
        loginText.visibility = View.VISIBLE
        passwordText.visibility = View.VISIBLE
        loginButton.visibility = View.VISIBLE
        fetchingNodesBar.visibility = View.GONE
    }

    override fun onClick(v: View) {
        if (v.id == R.id.button_login) {
            initLogin()
        }
    }

    private fun initLogin() {
        if (!validateForm()) {
            return
        }

        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        imm.hideSoftInputFromWindow(loginText.windowToken, 0)

        email = loginText.text.toString().lowercase(Locale.ENGLISH).trim()
        password = passwordText.text.toString()

        loginText.visibility = View.GONE
        passwordText.visibility = View.GONE
        loginButton.visibility = View.GONE

        title.text = resources.getString(R.string.logging_in)

        megaApi.login(email, password, this)
    }

    /**
     * Validate email and password
     */
    private fun validateForm(): Boolean {
        val emailError = getEmailError()
        val passwordError = getPasswordError()

        loginText.error = emailError
        passwordText.error = passwordError

        if (emailError != null) {
            loginText.requestFocus()
            return false
        } else if (passwordError != null) {
            passwordText.requestFocus()
            return false
        }
        return true
    }

    /**
     * Validate email
     */
    private fun getEmailError(): String? {
        val value = loginText.text.toString()
        if (value.isEmpty()) {
            return getString(R.string.error_enter_email)
        }
        if (!android.util.Patterns.EMAIL_ADDRESS.matcher(value).matches()) {
            return getString(R.string.error_invalid_email)
        }
        return null
    }

    /**
     * Validate password
     */
    private fun getPasswordError(): String? {
        val value = passwordText.text.toString()
        if (value.isEmpty()) {
            return getString(R.string.error_enter_password)
        }
        return null
    }

    override fun onRequestStart(api: MegaApiJava, request: MegaRequest) {
        log("onRequestStart: ${request.requestString}")

        when (request.type) {
            MegaRequest.TYPE_LOGIN -> {
                title.text = resources.getString(R.string.logging_in)
            }
            MegaRequest.TYPE_FETCH_NODES -> {
                title.text = resources.getString(R.string.fetching_nodes)
                fetchingNodesBar.visibility = View.VISIBLE
                fetchingNodesBar.layoutParams.width = 350
                fetchingNodesBar.progress = 0
            }
        }
    }

    override fun onRequestUpdate(api: MegaApiJava, request: MegaRequest) {
        log("onRequestUpdate: ${request.requestString}")

        if (request.type == MegaRequest.TYPE_FETCH_NODES) {
            fetchingNodesBar.visibility = View.VISIBLE
            fetchingNodesBar.layoutParams.width = 350
            if (request.totalBytes > 0) {
                var progressValue = 100.0 * request.transferredBytes / request.totalBytes
                if (progressValue > 99 || progressValue < 0) {
                    progressValue = 100.0
                    title.text = resources.getString(R.string.preparing_nodes)
                }
                log("progressValue = ${progressValue.toInt()}")
                fetchingNodesBar.progress = progressValue.toInt()
            }
        }
    }

    override fun onRequestFinish(api: MegaApiJava, request: MegaRequest, e: MegaError) {
        log("onRequestFinish: ${request.requestString}")

        when (request.type) {
            MegaRequest.TYPE_LOGIN -> {
                if (e.errorCode == MegaError.API_OK) {
                    megaApi.fetchNodes(this)
                } else {
                    var errorMessage = e.errorString
                    if (e.errorCode == MegaError.API_ENOENT) {
                        errorMessage = getString(R.string.error_incorrect_email_or_password)
                    }
                    Toast.makeText(this, errorMessage, Toast.LENGTH_LONG).show()

                    title.text = resources.getString(R.string.login_text)
                    loginText.visibility = View.VISIBLE
                    passwordText.visibility = View.VISIBLE
                    loginButton.visibility = View.VISIBLE
                }
            }
            MegaRequest.TYPE_FETCH_NODES -> {
                if (e.errorCode != MegaError.API_OK) {
                    Toast.makeText(this, e.errorString, Toast.LENGTH_LONG).show()
                    title.text = resources.getString(R.string.login_text)
                    loginText.visibility = View.VISIBLE
                    passwordText.visibility = View.VISIBLE
                    loginButton.visibility = View.VISIBLE
                    fetchingNodesBar.progress = 0
                    fetchingNodesBar.visibility = View.GONE
                } else {
                    val intent = Intent(this, NavigationActivity::class.java)
                    intent.flags = Intent.FLAG_ACTIVITY_CLEAR_TOP
                    startActivity(intent)
                    finish()
                }
            }
        }
    }

    override fun onRequestTemporaryError(api: MegaApiJava, request: MegaRequest, e: MegaError) {
        log("onRequestTemporaryError: ${request.requestString}")
    }

    companion object {
        fun log(message: String) {
            MegaApiAndroid.log(MegaApiAndroid.LOG_LEVEL_INFO, message, "MainActivity")
        }
    }
}

