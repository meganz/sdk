/*
 * SwingExample.java
 *
 * This file is part of the Mega SDK Java bindings example code.
 *
 * Created: 2015-04-30 Sergio Hernández Sánchez <shs@mega.co.nz>
 * Changed:
 *
 * (c) Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

package nz.mega.bindingsample;

import java.util.ArrayList;
import java.util.Locale;
import java.util.regex.Pattern;

import java.awt.HeadlessException;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;

import javax.swing.DefaultListModel;
import javax.swing.JButton;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JList;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JPasswordField;
import javax.swing.JScrollPane;
import javax.swing.JTextField;

import nz.mega.sdk.MegaApiJava;
import nz.mega.sdk.MegaApiSwing;
import nz.mega.sdk.MegaError;
import nz.mega.sdk.MegaLoggerInterface;
import nz.mega.sdk.MegaNode;
import nz.mega.sdk.MegaRequest;
import nz.mega.sdk.MegaRequestListenerInterface;

/**
 * A simple Java Swing UI example implementation that demonstrates accessing
 * Mega's file storage.
 * 
 * @author Sergio Hernández Sánchez
 */
public class SwingExample extends JFrame implements MegaRequestListenerInterface, MegaLoggerInterface {

    private static final long serialVersionUID = 1L;

    /** Reference to the Mega API object. */
    static MegaApiSwing megaApi = null;

    /**
     * Mega SDK application key.
     * Generate one for free here: https://mega.io/developers
     */
    static final String APP_KEY = "YYJwAIRI";
    
    /** User agent string used for HTTP requests to the Mega API. */
    static final String USER_AGENT = "MEGA Java Sample Demo SDK";

    // UI elements.
    static JPanel panel;
    static JTextField loginText;
    static JTextField passwordText;
    static JButton loginButton;
    static JScrollPane listFiles;
    static DefaultListModel<String> listModel;
    static JLabel statusLabel;

    // Some externalised strings.
    private static final String STR_APP_TITLE = "Java Bindings Example";
    private static final String STR_EMAIL_TEXT = "Email:";
    private static final String STR_PWD_TEXT = "Password:";
    private static final String STR_LOGIN_TEXT = "Login";
    private static final String STR_LOGOUT_TEXT = "Logout";
    private static final String STR_INITIAL_STATUS = "Please, enter your login details";
    private static final String STR_ERROR_ENTER_EMAIL = "Please, enter your email address";
    private static final String STR_ERROR_INVALID_EMAIL = "Invalid email address";
    private static final String STR_ERROR_ENTER_PWD = "Please, enter your password";
    private static final String STR_ERROR_INCORRECT_EMAIL_OR_PWD = "Incorrect email or password";
    private static final String STR_LOGGING_IN = "Logging in...";
    private static final String STR_FETCHING_NODES = "Fetching nodes...";
    private static final String STR_PREPARING_NODES = "Preparing nodes...";

    public SwingExample() throws HeadlessException {
        super(STR_APP_TITLE);

        initializeMegaApi();
        initializeGUI();
    }

    /**
     * Set logger and get reference to MEGA API
     */
    private void initializeMegaApi() {

        MegaApiSwing.addLoggerObject(this);
        MegaApiSwing.setLogLevel(MegaApiSwing.LOG_LEVEL_MAX);

        if (megaApi == null) {
            String path = System.getProperty("user.dir");
            megaApi = new MegaApiSwing(SwingExample.APP_KEY, SwingExample.USER_AGENT, path);
        }
    }

    /**
     * Create GUI components and configure them
     */
    private void initializeGUI() {
        // Create and set up the window.
        this.setSize(270, 160);
        this.setResizable(false);
        this.setLocationByPlatform(true);
        this.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);

        panel = new JPanel();
        this.add(panel);

        panel.setLayout(null);

        JLabel loginLabel = new JLabel(STR_EMAIL_TEXT);
        loginLabel.setBounds(10, 10, 80, 25);
        panel.add(loginLabel);

        loginText = new JTextField(20);
        loginText.setBounds(100, 10, 160, 25);
        panel.add(loginText);

        JLabel passwordLabel = new JLabel(STR_PWD_TEXT);
        passwordLabel.setBounds(10, 40, 80, 25);
        panel.add(passwordLabel);

        passwordText = new JPasswordField(20);
        passwordText.setBounds(100, 40, 160, 25);
        panel.add(passwordText);

        loginButton = new JButton(STR_LOGIN_TEXT);
        loginButton.setBounds(160, 70, 100, 25);
        panel.add(loginButton);

        loginButton.addActionListener(new ActionListener() {
            public void actionPerformed(ActionEvent e) {
                JButton loginButton = (JButton) e.getSource();

                if (loginButton.getText().compareTo(STR_LOGIN_TEXT) == 0) {
                    initLogin();
                } else if (loginButton.getText().compareTo(STR_LOGOUT_TEXT) == 0) {
                    initLogout();
                }
            }
        });
        getRootPane().setDefaultButton(loginButton);

        listModel = new DefaultListModel<String>();
        JList<String> list = new JList<String>(listModel);
        listFiles = new JScrollPane(list);
        listFiles.setBounds(10, 100, this.getWidth() - 20, 200);
        // panel.add(listFiles); // Do not add it yet

        statusLabel = new JLabel();
        statusLabel.setBounds(10, 110, this.getWidth() - 20, 15);
        setStatus(STR_INITIAL_STATUS);
        panel.add(statusLabel);

        this.setVisible(true);
    }

    /**
     * Change the design of Main window when a session is opened.
     * - Prevent user to modify 'email' and 'password'
     * - Rename button to 'Logout'
     * - Prevent user to click on button 'login'
     * - Show the component where nodes are listed
     */
    private void setLoggedInMode() {

        loginButton.setText(STR_LOGOUT_TEXT);
        loginButton.setEnabled(true); // It might be disabled if called during a login process
        loginText.setEnabled(false);
        passwordText.setEnabled(false);

        setSize(270, 360);
        statusLabel.setBounds(10, 310, getWidth() - 20, 15);
        panel.add(listFiles);
    }

    /**
     * Change the design of Main window when a session is closed:
     * - Allow user to modify 'email' and 'password'
     * - Rename button to 'Login'
     * - Allow user to click on button 'login'
     * - Hide the component where nodes are listed
     */
    private void setLoggedOutMode() {
        loginButton.setText(STR_LOGIN_TEXT);
        enableUserInteraction(true);

        setSize(270, 160);
        statusLabel.setBounds(10, 110, getWidth() - 20, 15);
        panel.remove(listFiles);
    }

    private void enableUserInteraction(boolean b) {
        loginText.setEnabled(b);
        passwordText.setEnabled(b);
        loginButton.setEnabled(b);
    }

    private void initLogin() {

        if (!validateForm()) {
            return;
        }

        enableUserInteraction(false);

        String email = loginText.getText().toString().toLowerCase(Locale.ENGLISH).trim();
        String password = passwordText.getText().toString();

        megaApi.login(email, password, this);
    }

    private boolean validateForm() {
        String emailError = getEmailError();
        String passwordError = getPasswordError();

        if (emailError != null) {
            JOptionPane.showMessageDialog(null, emailError);
            loginText.requestFocus();
            return false;
        } else if (passwordError != null) {
            JOptionPane.showMessageDialog(null, passwordError);
            passwordText.requestFocus();
            return false;
        }
        return true;
    }

    /**
     * Validate email: not empty and valid format
     */
    private String getEmailError() {
        String value = loginText.getText();
        if (value.length() == 0) {
            return STR_ERROR_ENTER_EMAIL;
        }
        if (!rfc2822.matcher(value).matches()) {
            return STR_ERROR_INVALID_EMAIL;
        }
        return null;
    }

    final static Pattern rfc2822 = Pattern
            .compile("[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*@(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?");

    /**
     * Validate password: not empty
     */
    private String getPasswordError() {
        String value = passwordText.getText();
        if (value.length() == 0) {
            return STR_ERROR_ENTER_PWD;
        }
        return null;
    }

    protected void initLogout() {
        megaApi.logout(this);

        setLoggedOutMode();
        setStatus(STR_LOGOUT_TEXT);
    }

    public static void main(String[] args) {
        // Schedule a job for the event-dispatching thread
        javax.swing.SwingUtilities.invokeLater(new Runnable() {
            public void run() {
                new SwingExample();
            }
        });
    }

    @Override
    public void onRequestStart(MegaApiJava api, MegaRequest request) {
        log("onRequestStart: " + request.getRequestString());

        if (request.getType() == MegaRequest.TYPE_LOGIN) {
            setStatus(STR_LOGGING_IN);
        } else if (request.getType() == MegaRequest.TYPE_FETCH_NODES) {
            setStatus(STR_FETCHING_NODES);
        }
    }

    @Override
    public void onRequestUpdate(MegaApiJava api, MegaRequest request) {
        log("onRequestUpdate: " + request.getRequestString());

        if (request.getType() == MegaRequest.TYPE_FETCH_NODES) {
            if (request.getTotalBytes() > 0) {
                double progressValue = 100.0 * request.getTransferredBytes() / request.getTotalBytes();
                if ((progressValue > 99) || (progressValue < 0)) {
                    progressValue = 100;
                }
                log("progressValue = " + (int) progressValue);
                setStatus(STR_PREPARING_NODES + String.valueOf((int) progressValue) + "%");
            }
        }
    }

    @Override
    public void onRequestFinish(MegaApiJava api, MegaRequest request, MegaError e) {
        log("onRequestFinish: " + request.getRequestString());

        if (request.getType() == MegaRequest.TYPE_LOGIN) {
            if (e.getErrorCode() == MegaError.API_OK) {
                megaApi.fetchNodes(this);
            } else {
                String errorMessage = e.getErrorString();
                if (e.getErrorCode() == MegaError.API_ENOENT) {
                    errorMessage = STR_ERROR_INCORRECT_EMAIL_OR_PWD;
                }

                JOptionPane.showMessageDialog(null, errorMessage);
                setStatus(errorMessage);

                // Enable user to change the credentials and hit 'login' again
                enableUserInteraction(true);
            }
        } else if (request.getType() == MegaRequest.TYPE_FETCH_NODES) {
            if (e.getErrorCode() != MegaError.API_OK) {
                JOptionPane.showMessageDialog(null, e.getErrorString());
                setStatus(e.getErrorString());

                // TODO: investigate errors from Request==TYPE_FETCH_NODES
                // and adapt the behaviour of GUI components below
                enableUserInteraction(true);

            } else {
                MegaNode parentNode = megaApi.getRootNode();
                ArrayList<MegaNode> nodes = megaApi.getChildren(parentNode);

                listModel.clear();
                MegaNode temp;
                for (int i = 0; i < nodes.size(); i++) {
                    temp = nodes.get(i);
                    listModel.addElement(temp.getName());
                }

                setLoggedInMode();
                setStatus("Done");
            }
        }
    }

    @Override
    public void onRequestTemporaryError(MegaApiJava api, MegaRequest request, MegaError e) {
        log("onRequestTemporaryError: " + request.getRequestString());
    }

    public static void log(String message) {
        MegaApiSwing.log(MegaApiSwing.LOG_LEVEL_INFO, message, "MainActivity");
    }

    @Override
    public void log(String time, int loglevel, String source, String message) {
        System.out.println("[" + time + "] " + message + " (Source: " + source + ")");
    }

    private void setStatus(String status) {
        statusLabel.setText(status);
    }
}
