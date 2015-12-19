Java Bindings Documentation

#Ubuntu/Linux#

Guide to installing and using Sphinx on Ubuntu/Linux
----------------------------------------------------

1. Install Sphinx
   Since Sphinx is written in the Python language, you need to install Python (the required version is at least 2.6) and        Sphinx. The latest Versions of Ubuntu and Fedora already have Python installed so please check this before you continue.

   First you will need to install the Sphinx package on your machine.
   
```bash   
   $ apt-get install python-sphinx
```   
   Installation instructions can be found here: [http://sphinx-doc.org/install.html](http://sphinx-doc.org/install.html)

2. Creating a Sphinx Directory 

   Sphinx needs to be actioned on a Directory, This directory will contain all of your files used with Sphinx.
   
   In you terminal cd to the location/folder you wish to use as your sphinx directory root folder. Next run the below:
   $ sphinx-quickstart 
   
   This will start the quickstart form, Please ensure the following options are selected/turned on:
   *Choose seperate build and source folders
   *Choose .rst as the source file format
   
   The rest of the choices are up to personal preference for simplicity you can choose yes to all of these.
   Information on this process can be found here: http://sphinx-doc.org/tutorial.html

3. Copy Files

   In the source directory contained withing the sdk/bindings/doc/java you will find the source folder please coply this        it's contents into your generated source folder. (*note if you are using the sdk/bindings/doc/java location you may not      need to perform this step).

4. Create the Documentation

   Once you have created your Sphinx directory and copied the files, you will run the command: 
   $ make html
   This is the command which converts the .rst files in the source folder and generates the html files.
   These files will then be placed in the build folder under html. 
   There are alo other formats that Sphinx can generate please see http://sphinx-doc.org/latest/tutorial.html

5. Viewing the documentation

   To view the documentation simply navigate into the build(/html) folder and open the index.html with your prefered web        browser. This will open the Documentation and you can browse this like a website.

For more info on Sphinx please see http://sphinx-doc.org/

#To Create PDF files of the Documentation.#
-------------------------------------------

1. Install TeXLive - Full

   TeXLive is used by Sphinx to convert the latex files into PDF

   You can use the command:
   sudo apt-get install wget build-essential python-old-doctools texlive-full
   
   However this requires quite a bit of space but will provide you with all the nessacary tools
   for Latex creation and modification.
   
2. Creating the PDF

   To create the PDFs, in terminal change the directory to the Sphinx directory and
   run the below command: 

   make latexpdf

   This will run the LaTeX builder and readily invokes the pdfTeX toolchain for you. 

   Please note if you do not install the TeXLive before running this 
   command you will receive error messages and only the latex will be produced not the pdfs.
   
   rst2pdf
   -------
   Alternativly there is a direct PDF creater call rst2pdf which you can use to convert
   your rst files into pdfs without the need to create the latex files.

PLEASE NOTE   
This is work in progress. As such there may be times when files are uploaded without/missing content.  
However You should always be able to build the documentation but results may vary from day to day.

