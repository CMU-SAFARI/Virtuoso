class Colors:
    BOLD = "\033[1m"
    RESET = "\033[0m"
    CYAN = "\033[96m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    MAGENTA = "\033[95m"
    BLUE = "\033[94m"
    WHITE = "\033[97m"

    @classmethod
    def disable(cls):
        cls.BOLD = ""
        cls.RESET = ""
        cls.CYAN = ""
        cls.GREEN = ""
        cls.YELLOW = ""
        cls.RED = ""
        cls.MAGENTA = ""
        cls.BLUE = ""
        cls.WHITE = ""


def print_header(title: str) -> None:
    line = "=" * (len(title) + 4)
    print(f"\n{Colors.BOLD}{line}")
    print(f"  {title}")
    print(f"{line}{Colors.RESET}\n")
