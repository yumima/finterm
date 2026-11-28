// Appended by ifw-setup.ps1 (GuiSetup) to app/packaging/installer/config/
// controlscript.qs — `--script` replaces the embedded control script, so the
// real one is run with this after it: it clicks the wizard through as a user
// would and answers the update's dialogs. The control script's own
// introduction-page update runs first; nothing here replaces it.

installer.setMessageBoxAutomaticAnswer("finterm.install.update", QMessageBox.Yes);
["finterm.install.update_failed", "finterm.install.update_incomplete", "finterm.install.in_use",
 "finterm.install.in_use_ask", "finterm.install.cannot_check", "TargetDirectoryInUse"].forEach(function(id) { installer.setMessageBoxAutomaticAnswer(id, QMessageBox.Ok); });
installer.setMessageBoxAutomaticAnswer("OverwriteTargetDirectory", QMessageBox.No);

function ciNext(page) { console.log("[ci] " + page); gui.clickButton(buttons.NextButton); }

var fintermIntroduction = Controller.prototype.IntroductionPageCallback;
Controller.prototype.IntroductionPageCallback = function()
{
    fintermIntroduction.call(this);
    if (!installer.value("FintermInstallRefused"))
        ciNext("introduction");
};
Controller.prototype.TargetDirectoryPageCallback = function() { ciNext("target " + installer.value("TargetDir")); };
Controller.prototype.ComponentSelectionPageCallback = function() { ciNext("components"); };
Controller.prototype.LicenseAgreementPageCallback = function()
{
    gui.currentPageWidget().AcceptLicenseCheckBox.setChecked(true); // IFW 4.7: packagemanagergui.cpp
    ciNext("license");
};
Controller.prototype.StartMenuDirectoryPageCallback = function() { ciNext("start menu"); };
Controller.prototype.ReadyForInstallationPageCallback = function() { ciNext("ready"); };
Controller.prototype.FinishedPageCallback = function()
{
    console.log("[ci] finished");
    gui.clickButton(buttons.FinishButton);
};
installer.installationFinished.connect(function() { gui.clickButton(buttons.NextButton); });
