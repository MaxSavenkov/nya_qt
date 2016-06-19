import QtQuick 2.0
import QtQuick.Controls 1.4
import QtQuick.Controls.Styles 1.4

Rectangle {
    width: 1024
    height: 768
    color: "transparent"

    Button {
        id: b1
        objectName: "b1"        
        width: 300
        x: 20
        y: 20
		text: "More magic!"

        style: ButtonStyle
        {
            background:	BorderImage {
                border { left: 92; top: 6; right: 17; bottom: 6; }
                anchors.fill: parent
                source: b1.pressed ? "images/menu_btn_pressed.png" : "images/menu_btn_normal.png"
            }

			label: Item {
					implicitWidth: row.implicitWidth
					implicitHeight: row.implicitHeight
					baselineOffset: row.y + text.y + text.baselineOffset
					Row {
						id: row
						anchors.centerIn: parent
						spacing: 2
						Image {
							source: control.iconSource
							anchors.verticalCenter: parent.verticalCenter
						}
						Text {
							id: text
							renderType: Text.QtRendering
							anchors.verticalCenter: parent.verticalCenter
							text: control.text
							color: "white"
						}
					}
			}
        }
    }

    Button {
        id: b2
        width: 300
        x: 20
        y: 80
        objectName: "b2"
        text: "Less magic!"
		style: ButtonStyle
		{
			background:	BorderImage {
    			border { left: 92; top: 6; right: 17; bottom: 6; }
    			anchors.fill: parent
    			source: b2.pressed ? "images/menu_btn_pressed.png" : "images/menu_btn_normal.png"
			}

			label: Item {
					implicitWidth: row.implicitWidth
					implicitHeight: row.implicitHeight
					baselineOffset: row.y + text.y + text.baselineOffset
					Row {
						id: row
						anchors.centerIn: parent
						spacing: 2
						Image {
							source: control.iconSource
							anchors.verticalCenter: parent.verticalCenter
						}
						Text {
							id: text
							renderType: Text.QtRendering
							anchors.verticalCenter: parent.verticalCenter
							text: control.text
							color: "white"
						}
					}
				}
		}
    }

    TextField {
        placeholderText: "Enter desired speed and press Enter"
        objectName: "t1"
        width: 200
        x: 20
        y: 150
    }

    Label {
        text: "FPS"
        x: 5
        y: 0
        width: 30
        color: "white"
        objectName: "l1"

		function setText( string )
		{
			text = string;
		}
    }
}
