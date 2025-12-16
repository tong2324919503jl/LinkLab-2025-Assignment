import sys
import subprocess
from pathlib import Path

# --- BOOTSTRAP: Auto-setup environment ---
import bootstrap
bootstrap.initialize()
# -----------------------------------------

# Now we can safely import rich
from rich.console import Console
from rich.panel import Panel
from rich.prompt import IntPrompt
from rich.table import Table
from rich import box

console = Console()

CONFIG_FILE = "cxx_std"
CANDIDATES = ["c++23", "c++20", "c++17"]

def check_support(std: str) -> bool:
    """Check if the current g++ supports the given standard."""
    try:
        cmd = ["g++", f"-std={std}", "-x", "c++", "-E", "/dev/null"]
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def get_compiler_version() -> str:
    try:
        result = subprocess.run(["g++", "--version"], capture_output=True, text=True)
        return result.stdout.splitlines()[0]
    except FileNotFoundError:
        return "g++ not found (Please install build-essential)"

def main():
    console.clear()
    console.print(Panel.fit(
        "[bold cyan]C++ Lab Environment Configurator[/bold cyan]\n[dim]Detects compiler support and generates config[/dim]", 
        box=box.ROUNDED
    ))
    
    compiler_ver = get_compiler_version()
    console.print(f"🖥️  Current Compiler: [bold]{compiler_ver}[/bold]\n")

    # 1. Detect support
    supported_options = []
    
    table = Table(show_header=True, header_style="bold magenta", box=box.SIMPLE)
    table.add_column("#", justify="center", style="bold yellow", width=4)
    table.add_column("Standard", style="cyan", width=12)
    table.add_column("Status", justify="left")
    
    with console.status("[bold green]Checking compiler support...[/bold green]"):
        index = 1
        for std in CANDIDATES:
            is_supported = check_support(std)
            if is_supported:
                status_icon = "[green]✔ Supported (Recommended)[/green]" if index == 1 else "[green]✔ Supported[/green]"
                table.add_row(str(index), std, status_icon)
                supported_options.append(std)
                index += 1
            else:
                table.add_row("-", std, "[dim red]✘ Not Supported (Upgrade compiler)[/dim red]")

    if not supported_options:
        console.print(Panel("[bold red]Error: Your compiler does not support C++17![/bold red]\nPlease upgrade g++ or install build-essential.", border_style="red"))
        sys.exit(1)

    console.print(table)
    
    # 2. Read current config
    current_config = None
    if Path(CONFIG_FILE).exists():
        current_config = Path(CONFIG_FILE).read_text().strip()
        console.print(f"\nCurrent Config: [bold yellow]{current_config}[/bold yellow]")

    # 3. Interactive Selection
    console.print("\n[bold]Select the C++ standard for this lab:[/bold]")
    
    choices_str = [str(i) for i in range(1, len(supported_options) + 1)]
    
    # Default to the first (highest) or existing config
    default_choice = "1"
    if current_config in supported_options:
        default_choice = str(supported_options.index(current_config) + 1)

    selected_index = IntPrompt.ask(
        "Enter selection", 
        choices=choices_str, 
        default=int(default_choice)
    )

    choice_std = supported_options[selected_index - 1]

    # 4. Save
    try:
        with open(CONFIG_FILE, "w") as f:
            f.write(choice_std)
        
        console.print(Panel(
            f"[bold green]Configuration Saved![/bold green]\n\n"
            f"Selected Version: [cyan]{choice_std}[/cyan]\n"
            f"Config File:      [yellow]{CONFIG_FILE}[/yellow]\n\n"
            f"[bold white on red] IMPORTANT [/bold white on red] You MUST commit this file:\n"
            f"> [bold]git add {CONFIG_FILE}[/bold]",
            border_style="green"
        ))
    except Exception as e:
        console.print(f"[bold red]Failed to write file: {e}[/bold red]")

if __name__ == "__main__":
    main()