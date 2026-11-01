public static class Entry
{
    public static void Main(string[] args)
    {
        Cli.RunCli(typeof({{class_name}}).GetMethod("{{function}}"), args);
    }
}
