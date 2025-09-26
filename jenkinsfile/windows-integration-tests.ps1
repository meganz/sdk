# This script runs Windows integration tests
# And monitor tests PIDs with procdump to core dump when an exception happens

Set-PSDebug -Trace 1

# We'll use BUILD_ID and APIURL_TO_TEST variables for CI
# Other CI env vars are optional
If (-Not ((Test-Path Env:BUILD_ID) -Or (Test-Path Env:APIURL_TO_TEST))) {
  echo "Both BUILD_ID and APIRUL_TO_TEST env vars should be defined."
  exit 1
}

$procdump = "C:\Tools\procdump.exe"
$cdb = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
$dumpDir = "dumps"

if (-not (Test-Path $dumpDir)) {
  New-Item -ItemType Directory -Path $dumpDir
}

# Monitor test PIDs to dump cores if necessary
# This will be running in background
Start-job -Name "childrenMonitor" -ScriptBlock {                                       
  # Get the testing processes PIDs and run procdump on them 
  $testPIDs = @{}
  while ($true) {
    $processes =  Get-Process -name test_integration_$Env:BUILD_ID -ErrorAction SilentlyContinue
    foreach ($proc in $processes) {
      # If the process is not in the array, run procdump on it and add it to the array 
      if (-not $testPIDs.ContainsKey($proc.Id)) {
        Start-Process $procdump -NoNewWindow -ArgumentList "-accepteula -ma -e 1 $($proc.Id) $dumpDir"
        $testPIDs[$proc.Id] = $true
      }
    }
    Start-Sleep -Milliseconds 100 # Get new test PIDs 10 times per second
  }
}

# Start the tests with a unique name, based on BUILD_ID, so we can monitor their PIDs
cp build_dir\tests\integration\Debug\test_integration.exe build_dir\tests\integration\Debug\test_integration_$Env:BUILD_ID.exe
$testProcess = Start-Process "build_dir\tests\integration\Debug\test_integration_$Env:BUILD_ID.exe" -PassThru -NoNewWindow -Wait -ArgumentList "--FREEACCOUNTS --CI --USERAGENT:$Env:USER_AGENT_TESTS_SDK --APIURL:$Env:APIURL_TO_TEST $Env:GTEST_FILTER $Env:GTEST_REPEAT $Env:TESTS_PARALLEL"
$testResult = $testProcess.ExitCode

# Stop monitoring child processes
Stop-job -Name "childrenMonitor"

# Compress the logs
If ( "$Env:TESTS_PARALLEL}" -ne $null ) {
  $pidDirs = Get-ChildItem -Path "." -Recurse -Filter "pid_*"
  foreach ($dir in $pidDirs) {
    gzip -c $dir/test_integration*.log > test_integration_${BUILD_ID}_${dir}.log.gz
  }
}
gzip -c test_integration.log > test_integration_${BUILD_ID}.log.gz
rm test_integration.log

# Analyse the dumps, if there's any
foreach ($dumpFile in Get-ChildItem -Path $dumpDir -Filter "*.dmp") {
  & $cdb -z $dumpDir\$dumpFile -c ".lines -e;kv;!analyze -v;q"
}

echo "Integration tests exit code: $testResult"
exit $testResult

