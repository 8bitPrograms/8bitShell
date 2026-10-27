import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BitShell

ColumnLayout {
    id: root
    spacing: 6
    function toAccount() {
        return kind.currentIndex === 0
            ? ({source: "identity", suffix: accountText.text, domain: domain.text})
            : ({source: "literal", name: accountText.text, domain: domain.text})
    }
    function load(rule) {
        kind.currentIndex = rule && rule.source === "literal" ? 1 : 0
        accountText.text = kind.currentIndex === 0 ? (rule.suffix || "") : (rule.name || "")
        domain.text = rule.domain || ""
    }
    AppComboBox {
        id: kind; objectName: "accountSourceCombo"; Layout.fillWidth: true
        model: ["Employee ID + suffix", "Fixed target account"]
    }
    AppTextField {
        id: accountText; objectName: "accountValueField"; Layout.fillWidth: true
        placeholderText: kind.currentIndex === 0 ? "Suffix, e.g. SYS" : "Account, e.g. opc"
    }
    AppTextField {
        id: domain; objectName: "accountDomainField"; Layout.fillWidth: true
        placeholderText: "Optional account domain (leave empty for opc)"
    }
}
