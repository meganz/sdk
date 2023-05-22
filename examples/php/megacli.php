#!/usr/bin/env php

<?php

ini_set('display_errors', 'On');
error_reporting(E_ERROR);

set_time_limit(0);
set_include_path(get_include_path() . PATH_SEPARATOR . "../../bindings/php");

include 'megaapi.php';

if (file_exists('vendor/autoload.php'))
    require_once('vendor/autoload.php');
else
    require_once('Symfony/autoload.php');

use Symfony\Component\Console\Shell;
use Symfony\Component\Console\Application; 
use Symfony\Component\Console\Command\Command;
use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;
use Symfony\Component\Console\Input\InputArgument;
use Symfony\Component\Console\Input\InputOption;

$megaapi = NULL;
$cwd = NULL;

class AppListener implements MegaListenerInterface
{
	public function onRequestStart($megaApi, $request)
	{
	}
		
	public function onRequestFinish($megaApi, $request, $error)
	{
		global $cwd;
		
		if($error->getErrorCode() != MegaError::API_OK) 
		{	
                	print("INFO: Request finished with error ( " . $request . " )   Result: " .  $error . "\n");
			print("MEGA > ");
			return;
		}

		$requestType = $request->getType();
		if($requestType == MegaRequest::TYPE_LOGIN)
		{
			print("Fetchning nodes. Please wait...\n");
	                print("MEGA > ");
			$megaApi->fetchNodes();
		}
		else if($requestType == MegaRequest::TYPE_FETCH_NODES)
		{
                        print("Account correctly loaded\n");
                        print("MEGA > ");
			$cwd = $megaApi->getRootNode();
		}
		else if($requestType == MegaRequest::TYPE_EXPORT)
		{
			print("INFO: Exported link: " . $request->getLink() . "\n");
			print("MEGA > ");
		}
		else if($requestType == MegaRequest::TYPE_ACCOUNT_DETAILS)
		{
			$accountDetails = $request->getMegaAccountDetails();
			print("INFO: Account details received\n"); 
			print("Account e-mail: " . $megaApi->getMyEmail() . "\n");
			print("Storage: " . $accountDetails->getStorageUsed() . " of " . $accountDetails->getStorageMax() . 
				" (" . (100 * $accountDetails->getStorageUsed() / $accountDetails->getStorageMax()) . "%)\n");
			print("Pro level: " . $accountDetails->getProLevel() . "\n");
			print("MEGA > ");
		}
	}

	public function onRequestTemporaryError($megaApi, $request, $error)
	{	
		print("INFO: Request temporary error ( " . $request . " )   Error: " + $error . "\n");
	}
	
	public function onTransferStart($megaApi, $transfer)
	{
		print("INFO: Transfer start ( " . $transfer . " " . $transfer->getFileName() . " )\n");
	}
	
	public function onTransferFinish($megaApi, $transfer, $error)
	{		
		print("INFO: Transfer finished ( " . $transfer . " " . $transfer->getFileName() . " )   Result: " . $error . "\n");
		print("MEGA > ");
	}
	
	public function onTransferUpdate($megaApi, $transfer)
	{
		print("INFO: Transfer update ( " . $transfer . " " . $transfer->getFileName() . " )   Progress: " . $transfer->getTransferredBytes()/1024 ." KB of " . $transfer->getTotalBytes()/1024 . " KB, " . $transfer->getSpeed()/1024 . " KB/s\n");
	}
			
	public function onTransferTemporaryError($megaApi, $request, $error)
	{		
		print("INFO: Transfer temporary error ( " . $transfer . " " . $transfer->getFileName() . " )   Error: " . $error . "\n");
	}
	
	public function onUsersUpdate($megaApi, $users)
	{
	}

	public function onNodesUpdate($megaApi, $nodes)
	{
		global $cwd;

		if($nodes != NULL) 
		{
			print("INFO: Nodes updated ( " . count($nodes) . " )\n"); 
			print("MEGA > ");
		}
		else
			$cwd = $megaApi->getRootNode();
	}
	
	public function onReloadNeeded($megaApi)
	{
		
	}
}

class LoginCommand extends Command
{
    protected function configure()
    {
        $this->setName('login');
        $this->setDescription('Log in to a MEGA account');
        $this->addArgument('email', InputArgument::REQUIRED, 'Email of the account');
        $this->addArgument('password', InputArgument::REQUIRED, 'Password of the account');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		
        $email = $input->getArgument('email');
        $password = $input->getArgument('password');
		$megaapi->login($email, $password);
    }
}

class LogoutCommand extends Command
{
    protected function configure()
    {
        $this->setName('logout');
        $this->setDescription('Log out a MEGA account');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		$megaapi->logout();
		$cwd = null;
    }
}

class LsCommand extends Command
{
    protected function configure()
    {
        $this->setName('ls');
        $this->setDescription('List a MEGA folder');
        $this->addArgument('path', InputArgument::OPTIONAL, 'folder path');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$path = $input->getArgument('path');
        if ($path) 
        {
            $folder = $megaapi->getNodeByPath($path, $cwd);
        } 
        else 
        {
            $folder = $cwd;
        }


		$output->writeln("    .");
		if($megaapi->getParentNode($folder) != NULL)
		{
			$output->writeln("    ..");
		}

		$children = $megaapi->getChildren($folder);
		foreach($children as $node)
		{
			$output->write("    " . $node->getName());
			if($node->getType() == MegaNode::TYPE_FILE)
			{
				$output->writeln("   (" . $node->getSize() . " bytes)");
			}
			else
			{
				$output->writeln("   (folder)");
			}
		}		
    }
}

class MkdirCommand extends Command
{
    protected function configure()
    {
        $this->setName('mkdir');
        $this->setDescription('Create a folder');
        $this->addArgument('name', InputArgument::REQUIRED, 'folder name');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$name = $input->getArgument('name');
		$megaapi->createFolder($name, $cwd);
    }
}

class CdCommand extends Command
{
    protected function configure()
    {
        $this->setName('cd');
        $this->setDescription('Change the current directory');
        $this->addArgument('path', InputArgument::REQUIRED, 'new current directory');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$path = $input->getArgument('path');
		$new = $megaapi->getNodeByPath($path, $cwd);
		if($new == null)
		{
			$output->writeln("Invalid path");
			return;
		}
		
		$cwd = $new;
    }
}

class MvCommand extends Command
{
    protected function configure()
    {
        $this->setName('mv');
        $this->setDescription('Move a file/folder');
        $this->addArgument('source', InputArgument::REQUIRED, 'Source file/folder');
        $this->addArgument('destination', InputArgument::REQUIRED, 'Destination file/folder');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$source = $input->getArgument('source');
		$name = $input->getArgument('destination');

		$srcNode = $megaapi->getNodeByPath($source, $cwd);
		if($srcNode == null)
		{
			$output->writeln($source . ": No such file or directory");
			return;
		}
		
		$dstNode = $megaapi->getNodeByPath($name, $cwd);
		if(($dstNode != null) && $dstNode->isFile())
		{
			$output->writeln($name . ": Not a directory");
			return;
		}
		
		if($dstNode != null)
		{
			$megaapi->moveNode($srcNode, $dstNode);
			return;
		}
		
		if(strpos($name,"/") !== false || strpos($name,"\\") !== false)
		{		
			$str1 = strrchr($name, "/");
			$str2 = strrchr($name, "\\");
			$index = null;
			
			if($str2 == FALSE || strlen($str1) < strlen($str2))
			{	
				echo "A\n";
				$index = strlen($name) - strlen($str1);
			}
			else
			{
				echo "B\n";
				$index = strlen($name) - strlen($str2);
			}
			
			$path = substr($name, 0, $index);
			$base = $megaapi->getNodeByPath($path, $cwd);
			$name = substr($name, $index+1);	
			
			echo "INDEX: " . $index . "\n";
			echo "str1: " . $str1 . "\n";
			echo "str2: " . $str2 . "\n";
			echo "PATH: " . $path . "\n";
			echo "NAME: " . $name . "\n";
						
			if($base == null)
			{
				$output->writeln($path . ": Not such directory");
				return;
			}	
								
			if($base->isFile())
			{
				$output->writeln($path . ": Not a directory");
				return;
			}
			
			$megaapi->moveNode($srcNode, $base);
			if(strlen($name) != 0)
			{	
				$megaapi->renameNode($srcNode, $name);
			}
			return;
		}
		
		if($dstNode == null)
		{
			$megaapi->renameNode($srcNode, $name); 
		}
    }
}

class PwdCommand extends Command
{
    protected function configure()
    {
        $this->setName('pwd');
        $this->setDescription('Get the current working directory');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$output->writeln($megaapi->getNodePath($cwd));
    }
}

class RmCommand extends Command
{
    protected function configure()
    {
        $this->setName('rm');
        $this->setDescription('Remove a file/folder');
        $this->addArgument('path', InputArgument::REQUIRED, 'Path to file/folder to delete');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$path = $input->getArgument('path');
		$node = $megaapi->getNodeByPath($path, $cwd);
		if($node == null)
		{
			$output->writeln("Invalid path");
			return;
		}
			
		$megaapi->remove($node);
    }
}

class GetCommand extends Command
{
    protected function configure()
    {
        $this->setName('get');
        $this->setDescription('Download a file from MEGA');
        $this->addArgument('path', InputArgument::REQUIRED, 'Path to the file');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$path = $input->getArgument('path');
		$node = $megaapi->getNodeByPath($path, $cwd);
		if($node == null)
		{
			$output->writeln("Invalid path");
			return;
		}
		
		if(!$node->isFile())
		{
			$output->writeln("Not a file");
			return;
		}
			
		$megaapi->startDownload($node
		, "./"	 /*local path*/
		, null 	 /*custom name*/
		, null	 /*app data*/
		, false	 /*start first*/
		, null	 /*cancel token*/
		, 3      /*collision check COLLISION_CHECK_FINGERPRINT*/
		, 2      /*collision resolution COLLISION_RESOLUTION_NEW_WITH_N*/
		, null); /*listener*/
    }
}

class PutCommand extends Command
{
    protected function configure()
    {
        $this->setName('put');
        $this->setDescription('Upload a file to MEGA');
        $this->addArgument('path', InputArgument::REQUIRED, 'Path to the local file');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$path = $input->getArgument('path');
		$megaapi->startUpload($path
                ,$cwd           /*parent node*/
		, null  	/*filename*/
		, 0     	/*mtime*/
		, null  	/*appData*/
		, false 	/*isSourceTemporary*/
		, false 	/*startFirst*/
		, null		/*cancelToken*/
		, null);  	/*listener*/
    }
}

class ExportCommand extends Command
{
    protected function configure()
    {
        $this->setName('export');
        $this->setDescription('Generate a public link');
        $this->addArgument('path', InputArgument::REQUIRED, 'Path to the file/folder in MEGA');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$path = $input->getArgument('path');
		$node = $megaapi->getNodeByPath($path, $cwd);
		if($node == null)
		{
			$output->writeln("Invalid path");
			return;
		}
			
		$megaapi->exportNode($node);
    }
}

class ImportCommand extends Command
{
    protected function configure()
    {
        $this->setName('import');
        $this->setDescription('Import a MEGA public file link');
        $this->addArgument('link', InputArgument::REQUIRED, 'Public MEGA file link');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$link = $input->getArgument('link');
		$megaapi->importFileLink($link, $cwd);
    }
}

class PasswdCommand extends Command
{
    protected function configure()
    {
        $this->setName('passwd');
        $this->setDescription('Change the access password');
        $this->addArgument('current_password', InputArgument::REQUIRED, 'Current password');
        $this->addArgument('new_password', InputArgument::REQUIRED, 'New password');
        $this->addArgument('repeat_new_password', InputArgument::REQUIRED, 'New password');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$current_password = $input->getArgument('current_password');
		$new_password = $input->getArgument('new_password');
		$repeat_new_password = $input->getArgument('repeat_new_password');

		if($new_password != $repeat_new_password)
		{
			$output->writeln("Error: Password mismatch");
			return;
		}

		$megaapi->changePassword($current_password, $new_password);
    }
}

class WhoamiCommand extends Command
{
    protected function configure()
    {
        $this->setName('whoami');
        $this->setDescription('Show info about the current user');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$output->writeln($megaapi->getMyEmail());		
		$megaapi->getAccountDetails();
    }
}

class MountCommand extends Command
{
    protected function configure()
    {
        $this->setName('mount');
        $this->setDescription('Show incoming shares');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		global $megaapi;
		global $cwd;
		
		if($cwd == null)
		{
			$output->writeln("Not logged in");
			return;
		}
		
		$output->writeln("INFO: INSHARES:");
		$users = $megaapi->getContacts();
		foreach($users as $user)
		{
			$megaapi->getInShares();
			$inshares = $megaapi->getInShares($user);
			foreach($inshares as $share)
			{
				$output->writeln("INFO: INSHARE on " . $user->getEmail() . " " . $share->getName() . " Access level: " . $megaapi->getAccess($share));
			}
		}
    }
}

class ExitCommand extends Command
{
    protected function configure()
    {
        $this->setName('exit');
        $this->setDescription('Exit the app');
    }

    protected function execute(InputInterface $input, OutputInterface $output)
    {
		exit(0);
	}
}

class QuitCommand extends ExitCommand
{
    protected function configure()
    {
        $this->setName('quit');
        $this->setDescription('Exit the app');
    }
}

MegaApi::setLogLevel(MegaApi::LOG_LEVEL_ERROR);
MegaApi::setLogToConsole(true);

$applistener = new AppListener();

$megaapi = new MegaApiPHP("API_KEY", "PHP megacli", ".");
$megaapi->addListener($applistener);

$application  = new Application('MEGA', 'PHP');
$application->add(new LoginCommand());
$application->add(new LogoutCommand());
$application->add(new LsCommand());
$application->add(new MkdirCommand());
$application->add(new CdCommand());
$application->add(new PwdCommand());
$application->add(new RmCommand());
$application->add(new GetCommand());
$application->add(new PutCommand());
$application->add(new ExitCommand());
$application->add(new QuitCommand());
$application->add(new ExportCommand());
$application->add(new ImportCommand());
$application->add(new WhoamiCommand());
$application->add(new PasswdCommand());
$application->add(new MountCommand());
$application->add(new MvCommand());

$shell = new Shell($application);
$shell->run();

?>


