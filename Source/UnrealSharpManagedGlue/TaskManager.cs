using System.Collections.Generic;
using System.Threading.Tasks;
using EpicGames.UHT.Utils;
using UnrealSharpManagedGlue.Utilities;

namespace UnrealSharpManagedGlue;

public static class TaskManager
{
	private static readonly List<Task> Tasks = new();

	public static void StartTask(UhtExportTaskDelegate action)
	{
		// UHT catches whatever escapes an export task and reports a bare ICE with no stack trace,
		// so log the real exception before letting it propagate.
		UhtExportTaskDelegate wrapped = factory =>
		{
			try
			{
				action(factory);
			}
			catch (System.Exception ex)
			{
				ConsoleUtilities.Log("Exception in glue export task:");
				ConsoleUtilities.Log(ex.ToString());
				throw;
			}
		};

		Task? task = GeneratorStatics.Factory.CreateTask(wrapped);

		if (task == null)
		{
			// Task execution was done synchronously
			return;
		}
		
		Tasks.Add(task);
	}
	
	public static void WaitForTasks()
	{
		Task.WaitAll(Tasks.ToArray());
	}
}