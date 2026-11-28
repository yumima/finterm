// The installer's own GUI, clicked through as a user would (IFW controller
// script: `setup.exe --script ifw-gui-install.qs`). CI uses it for updating
// over an existing install: only the GUI loads the component scripts — whose
// constructor removes the installed copy — before IFW checks the target
// folder; the command line checks it first and refuses.

function Controller()
{
    // The update question (installscript.qs): the answer a user gives.
    installer.setMessageBoxAutomaticAnswer("finterm.install.update", QMessageBox.Yes);
    // IFW's own "the folder is not empty — continue?": a user would be asked
    // this; answered No, so CI fails instead of passing over it.
    installer.setMessageBoxAutomaticAnswer("OverwriteTargetDirectory", QMessageBox.No);
    installer.installationFinished.connect(function() { gui.clickButton(buttons.NextButton); });
}

function next(page) { console.log("[ci] " + page); gui.clickButton(buttons.NextButton); }

Controller.prototype.IntroductionPageCallback = function() { next("introduction"); };
Controller.prototype.TargetDirectoryPageCallback = function() { next("target " + installer.value("TargetDir")); };
Controller.prototype.ComponentSelectionPageCallback = function() { next("components"); };
Controller.prototype.LicenseAgreementPageCallback = function()
{
    gui.currentPageWidget().AcceptLicenseRadioButton.setChecked(true);
    next("license");
};
Controller.prototype.StartMenuDirectoryPageCallback = function() { next("start menu"); };
Controller.prototype.ReadyForInstallationPageCallback = function() { next("ready"); };
Controller.prototype.FinishedPageCallback = function()
{
    console.log("[ci] finished");
    gui.clickButton(buttons.FinishButton);
};
