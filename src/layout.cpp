#include <errno.h>
#include "layout.h"
#include "config.h"

//initialize things and set up an icon  :)
LayoutManager::LayoutManager( bool useTrayIcon, const QString &devdir, const QString &settingsDir )
    : devdir(devdir), settingsDir(settingsDir),
      layoutGroup(new QActionGroup(this)),
      titleAction(new QAction(this)),
      updateDevicesAction(new QAction("Update &Joystick Devices",this)),
      updateLayoutsAction(new QAction("Update &Layout List",this)),
      quitAction(new QAction("&Quit",this)),
      le(0) {
    //prepare the popup first.
    titleAction->setEnabled(false);
    fillPopup();

    //make a tray icon
    if (useTrayIcon) {
        QSystemTrayIcon *tray = new QSystemTrayIcon(this);
        tray->setContextMenu(&trayMenu);
        tray->setIcon(QIcon(QJOYPAD_ICON24));
        tray->show();
        connect(tray, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayClick(QSystemTrayIcon::ActivationReason)));
    }
    //or make a floating icon
    else {
        FloatingIcon* icon = new FloatingIcon(QPixmap(QJOYPAD_ICON64),&trayMenu,0,"tray");
        connect(icon, SIGNAL( clicked()), this, SLOT( iconClick()));
        connect(icon, SIGNAL( closed()), qApp, SLOT( quit()));
        icon->show();
    }

    connect(updateLayoutsAction, SIGNAL(triggered()), this, SLOT(fillPopup()));
    connect(updateDevicesAction, SIGNAL(triggered()), this, SLOT(updateJoyDevs()));
    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));

    //no layout loaded at start.
    setLayoutName(NL);
}

LayoutManager::~LayoutManager() {
    if (le) {
        le->close();
        le = 0;
    }
}

QString LayoutManager::getFileName(const QString& layoutname ) {
    return QString("%1%2.lyt").arg(settingsDir, layoutname);
}

bool LayoutManager::load(const QString& name) {
    //it's VERY easy to load NL  :)
    if (name == NL) {
        clear();
        return true;
    }
    QFile file(getFileName(name));

    //if the file isn't available,
    if (!file.exists()) {
        errorBox("Load error","Failed to find a layout named " + name);
        return false;
    }

    //if the file isn't readable,
    if (!file.open(QIODevice::ReadOnly)) {
        errorBox("Load error","Error reading from file " + file.fileName());
        return false;
    }

    //reset all the joypads.
    //note that we don't use available here, but joypads instead. This is so
    //if one layout has more joypads than this one does, this won't have the
    //extra settings left over after things are supposed to be "cleared"
    foreach (JoyPad *joypad, joypads) {
        joypad->toDefault();
    }

    //start reading joypads!
    QTextStream stream( &file );
    bool okay = false;
    int num = 0;
    QChar ch = 0;
    QString word;

    while (!stream.atEnd()) {
        stream >> word;

        if (word.isNull())
            break;

        //if this line is specifying a joystick
        if (word.compare(QLatin1String("joystick"), Qt::CaseInsensitive) == 0) {
            stream >> word;
            num = word.toInt(&okay);
            //make sure the number of the joystick is valid
            if (!okay || num < 1) {
                errorBox( "Load error", QString("Error reading joystick definition. Unexpected token \"%1\". Expected a positive number.").arg(word));
                if (name != currentLayout) reload();
                else clear();
                return false;
            }
            stream.skipWhiteSpace();
            stream >> ch;
            if (ch != QChar('{')) {
                errorBox( "Load error", QString("Error reading joystick definition. Unexpected character \"%1\". Expected '{'.").arg(ch));
                if (name != currentLayout) reload();
                else clear();
                return false;
            }
            int index = num - 1;
            //if there was no joypad defined for this index before, make it now!
            if (joypads[index] == 0) {
                joypads.insert(index, new JoyPad(index, -1, this));
            }
            //try to read the joypad, report error on fail.
            if (!joypads[index]->readConfig(stream)) {
                errorBox( "Load error", QString("Error reading definition for joystick %1.").arg(index));
                //if this was attempting to change to a new layout and it failed,
                //revert back to the old layout.
                if (name != currentLayout) reload();
                //to keep from going into an infinite loop, if there is no good
                //layout to fall back on, go to NL.
                else clear();
                return false;
            }
        }
        else if (word.startsWith('#')) {
            // ignore comment
            stream.readLine();
        }
        else {
            errorBox("Load error", QString("Error reading joystick definition. Unexpected token \"%1\". Expected \"Joystick\".").arg(word));
            if (name != currentLayout) reload();
            else clear();
            return false;
        }
    }

    //if loading succeeded, this is our new layout.
    setLayoutName(name);
    return true;
}

bool LayoutManager::load() {
    //try to load the file named "layout" to retrieve the last used layout name
    QFile file( settingsDir + "layout");
    QString name;
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        name = stream.readLine();
        file.close();
        //if there was no name, don't load.
        if (name.isEmpty()) {
            return false;
        }
        //if there was a name, try to load it! Note, this will still return
        //false if the name is invalid ( see load() )
        return load(name);
    }
    //if the file isn't available to open, don't load.
    return false;
}

bool LayoutManager::reload() {
    return load(currentLayout);
}

void LayoutManager::clear() {
    //reset all the joypads...
    foreach (JoyPad *joypad, joypads) {
        joypad->toDefault();
    }
    //and call our layout NL
    setLayoutName(NL);
}

void LayoutManager::save() {
    if (currentLayout == NL) {
        saveAs();
        return;
    }

    //get a filename
    QString filename = getFileName( currentLayout );
    QFile file(filename);
    //if it's good, start writing the file
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream stream( &file );
        stream << "# " QJOYPAD_NAME " Layout File\n\n";
        foreach (JoyPad *joypad, joypads) {
            joypad->write( stream );
        }
        file.close();
    }
    //if it's not, error.
    else
        errorBox("Save error", "Could not open file " + filename + ", layout not saved.");
}


void LayoutManager::saveAs() {
    bool ok;
    //request a new name!
    QString name = QInputDialog::getText(0, QJOYPAD_NAME" - Name new layout","Enter a name for the new layout:", QLineEdit::Normal, QString::null, &ok );
    if (!ok) {
        return;
    }
    QFile file(settingsDir + name + ".lyt");
    //don't overwrite an existing layout.
    if (file.exists()) {
        errorBox("Save error", "That name's already taken!");
        return;
    }

    //now that the new layout has a name, that is the name we should use.
    setLayoutName(name);

    //since we have a new name for this layout now, we can save it normally  :)
    save();

    //add the new name to our lists
    fillPopup();
    if (le) {
        le->updateLayoutList();
    }
}

void LayoutManager::saveDefault() {
    QFile file( settingsDir + "layout");
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream(&file) << currentLayout;
        file.close();
    }
}

void LayoutManager::remove() {
    if (currentLayout == NL) return;
    if (QMessageBox::warning( 0, QJOYPAD_NAME" - Delete layout?","Remove layout permanently from your hard drive?", "Yes", "No", 0, 0, 1 ) == 1) return;
    QString filename = getFileName( currentLayout );
    if (!QFile(filename).remove()) {
        errorBox("Remove error", "Could not remove file " + filename);
    }
    fillPopup();

    if (le) {
        le->updateLayoutList();
    }
    clear();
}

QStringList LayoutManager::getLayoutNames() const {
    //goes through the list of .lyt files and removes the file extensions ;)
    QStringList result = QDir(settingsDir).entryList(QStringList("*.lyt"));

    for (int i = 0; i < result.size(); ++ i) {
        QString& name = result[i];
        name.truncate(name.length() - 4);
    }
    //and, of course, there's always NL.
    result.prepend(NL);

    return result;
}

void LayoutManager::setLayoutName(const QString& name) {
    QList<QAction*> actions = layoutGroup->actions();
    bool found = false;
    for (int i = 0; i < actions.size(); ++ i) {
        QAction* action = actions[i];
        if (action->data().toString() == name) {
            action->setChecked(true);
            found = true;
            break;
        }
    }

    currentLayout = found ? name : NL;

    if (le) {
        le->setLayout(name);
    }
}

void LayoutManager::iconClick() {
    //don't show the dialog if there aren't any joystick devices plugged in
    if (available.isEmpty()) {
        errorBox("No joystick devices available","No joystick devices are currently available to configure.\nPlease plug in a gaming device and select\n\"Update Joystick Devices\" from the popup menu.");
        return;
    }
    if (le) {
        if (le->isActiveWindow()) {
            le->close();
        }
        else {
            le->activateWindow();
        }
        return;
    }
    //otherwise, make a new LayoutEdit dialog and show it.
    le = new LayoutEdit(this);
    le->setLayout(currentLayout);
}

void LayoutManager::trayClick(QSystemTrayIcon::ActivationReason reason) {
    if(reason == QSystemTrayIcon::Trigger) {
        iconClick();
    }
}

void LayoutManager::layoutTriggered() {
    QAction *action = qobject_cast<QAction*>(sender());
    //if they clicked on a Layout name, load it!
    if (action) {
        load(action->data().toString());
    }
}

void LayoutManager::fillPopup() {
    //start with an empty slate
    trayMenu.clear();

    //make a list of joystick devices
    QString title = "Joysticks: ";
    foreach (JoyPad *joypad, available) {
        title += QString("%1 ").arg(joypad->getIndex() + 1);
    }

    trayMenu.setTitle(title);
    titleAction->setText(title);
    trayMenu.addAction(titleAction);
    trayMenu.addSeparator();

    //add in the Update options
    trayMenu.addAction(updateLayoutsAction);
    trayMenu.addAction(updateDevicesAction);
    trayMenu.addSeparator();

    //then add all the layout names
    QStringList names = getLayoutNames();
    foreach (const QString &name, names) {
        QString title = name;
        title.replace('&',"&&");
        QAction *action = trayMenu.addAction(title);
        action->setData(name);
        action->setCheckable(true);
        action->setActionGroup(layoutGroup);
        //put a check by the current one  ;)
        if (currentLayout == name) {
            action->setChecked(true);
        }
        connect(action, SIGNAL(triggered()), this, SLOT(layoutTriggered()));
    }
    trayMenu.addSeparator();

    //and, at the end, quit!
    trayMenu.addAction(quitAction);
}

void LayoutManager::updateJoyDevs() {
    debug_mesg("updating joydevs\n");

    //reset all joydevs to sentinal value (-1)
    foreach (JoyPad *joypad, joypads) {
        joypad->close();
    }

    //clear out the list of previously available joysticks
    available.clear();

    //set all joydevs anew (create new JoyPad's if necesary)
    QDir deviceDir(devdir);
    QStringList devices = deviceDir.entryList(QStringList("js*"), QDir::System);
    QRegExp devicename(".*\\js(\\d+)");
    int joydev = -1;
    int index = -1;
    //for every joystick device in the directory listing...
    //(note, with devfs, only available devices are listed)
    foreach (const QString &device, devices) {
        QString devpath = QString("%1/%2").arg(devdir, device);
        debug_mesg("found a device file, %s\n", qPrintable(devpath));
        //try opening the device.
        joydev = open( qPrintable(devpath), O_RDONLY | O_NONBLOCK);
        //if it worked, then we have a live joystick! Make sure it's properly
        //setup.
        if (joydev >= 0) {
            devicename.indexIn(device);
            index = devicename.cap(1).toInt();
            JoyPad* joypad = joypads[index];
            //if we've never seen this device before, make a new one!
            if (joypad == 0) {
                joypad = new JoyPad( index, joydev, this );
                joypads.insert(index,joypad);
            }
            else {
                debug_mesg("found previously open joypad with index %d, ignoring", index);
                joypad->open(joydev);
            }
            //make this joystick device available.
            available.insert(index,joypad);
        }
        else {
            perror(qPrintable(devpath));
        }
    }
    //when it's all done, rebuild the popup menu so it displays the correct
    //information.
    fillPopup();
    if(le) {
        le->updateJoypadWidgets();
    }
    debug_mesg("done updating joydevs\n");
}
