Java Bindings Documentation

To create the Documentation html files using Sphinx please follow the below.

1. Install Sphinx

First you will need to install the Sphinx package on you machine.
Installation instructions can be found here: http://sphinx-doc.org/install.html

For Linux this is done by running $ apt-get install python-sphinx
Please check the install guide for Windows and Mac OS installation.

2. Creating a Sphinx Directory 

Next you will need to create a Sphinx Directory
Information on this process can be found here: http://sphinx-doc.org/tutorial.html

For Linux simply run sphinx-quickstart from your chosen location for the directory.
This command will run through the initial set up options for sphinx.

3. Copy Files

Now you will copy the files from the java/sphinx/ folder into your directory. This will give you all the files needed to generate the html files.

4. Create the Documentation

Once you have created your Sphinx directory and copied the files, you will run the command make html
This is the command which converts the .rst files in the source folder and generates the html files.
These files will then be placed in the build folder under html.

If you have chosen to use one _build folder then all these files will be in this folder.

5. Viewing the documentation
To view the documentation simply navigate into the html folder and open the index.html with your prefered web browser.

For more info on Sphinx please see http://sphinx-doc.org/

If you wish you can simply read the reStructuredText files in the source folder.  
These can also be used to generate other formats.

PLEASE NOTE   
This is work in progress. As such there may be times when files are uploaded without/missing content.  
However You should always be able to build the documentation but results may vary from day to day.

