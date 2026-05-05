from groundstation.ui.mastercontrol_app import MastercontrolApp


def main() -> None:
    app = MastercontrolApp(simulate=False)
    try:
        app.update_idletasks()
        app.update()
        app._set_map_zoom(app.map_zoom + 1, quiet=True)
        app.update_idletasks()
        app.update()
        app._set_map_zoom(app.map_zoom - 1, quiet=True)
        app.update_idletasks()
        app.update()
    finally:
        app._on_close()

    print("PASS: mastercontrol smoke")


if __name__ == "__main__":
    main()
