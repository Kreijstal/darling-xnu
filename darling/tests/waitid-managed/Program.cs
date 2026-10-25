using System;
using System.Diagnostics;
class ProcessProbe {
 static int Main() {
  for(int i=0;i<3;i++) {
   using var child = Process.Start(new ProcessStartInfo("/bin/sh") { ArgumentList = { "-c", "exit 42" }, UseShellExecute=false });
   if(child == null || !child.WaitForExit(5000) || child.ExitCode != 42) {Console.WriteLine("child wait or status failed");return 1;}
  }
  Console.WriteLine("Managed Process.Start/WaitForExit passed, exit42 three times");return 0;
 }
}
