/**
 * MainActivity.java
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
package nz.mega.android.bindingsample;

import java.util.Locale;

import nz.mega.sdk.MegaApiAndroid;
import nz.mega.sdk.MegaApiJava;
import nz.mega.sdk.MegaError;
import nz.mega.sdk.MegaRequest;
import nz.mega.sdk.MegaRequestListenerInterface;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.AsyncTask;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity implements OnClickListener, MegaRequestListenerInterface {

	TextView title;
	EditText loginText;
	EditText passwordText;
	Button loginButton;
	ProgressBar fetchingNodesBar;
	
	String email = "";
	String password = "";
	
	String gPublicKey = "";
	String gPrivateKey = "";
	
	MegaApiAndroid megaApi;
	
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		
		super.onCreate(savedInstanceState);
		
		DemoAndroidApplication app = (DemoAndroidApplication) getApplication();
		megaApi = app.getMegaApi();
		
		setContentView(R.layout.activity_main);
		
		title = (TextView) findViewById(R.id.title_text_view);
		loginText = (EditText) findViewById(R.id.email_text);
		passwordText = (EditText) findViewById(R.id.password_text);
		loginButton = (Button) findViewById(R.id.button_login);
		loginButton.setOnClickListener(this);
		fetchingNodesBar = (ProgressBar) findViewById(R.id.fetching_nodes_bar);
		
		title.setText(getResources().getString(R.string.login_text));
		loginText.setVisibility(View.VISIBLE);
		passwordText.setVisibility(View.VISIBLE);
		loginButton.setVisibility(View.VISIBLE);
		fetchingNodesBar.setVisibility(View.GONE);
	}

	@Override
	public void onClick(View v) {
		if (v.getId() == R.id.button_login) {
			initLogin();
		}
	}
	
	private void initLogin(){
		
		if (!validateForm()) {
			return;
		}
		
		InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
		imm.hideSoftInputFromWindow(loginText.getWindowToken(), 0);
		
		email = loginText.getText().toString().toLowerCase(Locale.ENGLISH).trim();
		password = passwordText.getText().toString();
		
		loginText.setVisibility(View.GONE);
		passwordText.setVisibility(View.GONE);
		loginButton.setVisibility(View.GONE);
		
		title.setText(getResources().getString(R.string.logging_in));

		megaApi.login(email, password, this);
	}
	
	/*
	 * Validate email and password
	 */
	private boolean validateForm() {
		String emailError = getEmailError();
		String passwordError = getPasswordError();

		loginText.setError(emailError);
		passwordText.setError(passwordError);

		if (emailError != null) {
			loginText.requestFocus();
			return false;
		} else if (passwordError != null) {
			passwordText.requestFocus();
			return false;
		}
		return true;
	}
	
	/*
	 * Validate email
	 */
	private String getEmailError() {
		String value = loginText.getText().toString();
		if (value.length() == 0) {
			return getString(R.string.error_enter_email);
		}
		if (!android.util.Patterns.EMAIL_ADDRESS.matcher(value).matches()) {
			return getString(R.string.error_invalid_email);
		}
		return null;
	}
	
	/*
	 * Validate password
	 */
	private String getPasswordError() {
		String value = passwordText.getText().toString();
		if (value.length() == 0) {
			return getString(R.string.error_enter_password);
		}
		return null;
	}

	@Override
	public void onRequestStart(MegaApiJava api, MegaRequest request) {
		log("onRequestStart: " + request.getRequestString());
		
		if (request.getType() == MegaRequest.TYPE_LOGIN){
			title.setText(getResources().getString(R.string.logging_in));
		}
		else if (request.getType() == MegaRequest.TYPE_FETCH_NODES){
			title.setText(getResources().getString(R.string.fetching_nodes));
			fetchingNodesBar.setVisibility(View.VISIBLE);
			fetchingNodesBar.getLayoutParams().width = 350;
			fetchingNodesBar.setProgress(0);
		}
	}

	@Override
	public void onRequestUpdate(MegaApiJava api, MegaRequest request) {
		log("onRequestUpdate: " + request.getRequestString());
		
		if (request.getType() == MegaRequest.TYPE_FETCH_NODES){
			fetchingNodesBar.setVisibility(View.VISIBLE);
			fetchingNodesBar.getLayoutParams().width = 350;
			if (request.getTotalBytes() > 0){
				double progressValue = 100.0 * request.getTransferredBytes() / request.getTotalBytes();
				if ((progressValue > 99) || (progressValue < 0)){
					progressValue = 100;
					title.setText(getResources().getString(R.string.preparing_nodes));
				}
				log("progressValue = " + (int)progressValue);
				fetchingNodesBar.setProgress((int)progressValue);				
			}
		}
	}

	@Override
	public void onRequestFinish(MegaApiJava api, MegaRequest request,
			MegaError e) {
		log("onRequestFinish: " + request.getRequestString());
		
		if (request.getType() == MegaRequest.TYPE_LOGIN){
			if (e.getErrorCode() == MegaError.API_OK){
				megaApi.fetchNodes(this);
			}
			else{
				String errorMessage = e.getErrorString();
				if (e.getErrorCode() == MegaError.API_ENOENT) {
					errorMessage = getString(R.string.error_incorrect_email_or_password);
				}
				Toast.makeText(this, errorMessage, Toast.LENGTH_LONG).show();
				
				title.setText(getResources().getString(R.string.login_text));
				loginText.setVisibility(View.VISIBLE);
				passwordText.setVisibility(View.VISIBLE);
				loginButton.setVisibility(View.VISIBLE);
			}
		}
		else if (request.getType() == MegaRequest.TYPE_FETCH_NODES){
			if (e.getErrorCode() != MegaError.API_OK) {
				Toast.makeText(this, e.getErrorString(), Toast.LENGTH_LONG).show();
				title.setText(getResources().getString(R.string.login_text));
				loginText.setVisibility(View.VISIBLE);
				passwordText.setVisibility(View.VISIBLE);
				loginButton.setVisibility(View.VISIBLE);
				fetchingNodesBar.setProgress(0);
				fetchingNodesBar.setVisibility(View.GONE);
			}
			else{
				Intent intent = new Intent(this, NavigationActivity.class);
				intent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
				startActivity(intent);
				finish();
			}
		}
	}

	@Override
	public void onRequestTemporaryError(MegaApiJava api, MegaRequest request,
			MegaError e) {
		log("onRequestTemporaryError: " + request.getRequestString());
	}
	
	public static void log(String message) {
		MegaApiAndroid.log(MegaApiAndroid.LOG_LEVEL_INFO, message, "MainActivity");
	}
}
