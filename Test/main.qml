import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

import Qt3D.Core 2.0
import Qt3D.Render 2.0
import Qt3D.Input 2.0
import Qt3D.Extras 2.15
import QtQuick.Scene3D 2.0
import Qt3D.Render 2.15

Rectangle{
    color: "#ff00ff"

    Text {
        id: name
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom

        text: qsTr("3d压力测试")

        font.pixelSize: 100
    }

    Scene3D {
        id: scene3d
        anchors.fill: parent
        anchors.margins: 10
        focus: true
        aspects: ["input", "logic"]
        cameraAspectRatioMode: Scene3D.AutomaticAspectRatio

        Entity {
            id: sceneRoot
            property RenderCapabilities capabilities : renderSettings.renderCapabilities

            Camera {
                id: camera
                projectionType: CameraLens.PerspectiveProjection
                fieldOfView: 45
                nearPlane : 0.1
                farPlane : 1000.0
                position: Qt.vector3d( 0.0, 0.0, 40.0 )
                upVector: Qt.vector3d( 0.0, 1.0, 0.0 )
                viewCenter: Qt.vector3d( 0.0, 0.0, 0.0 )
            }

            FirstPersonCameraController { camera: camera }

            components: [
                RenderSettings {
                    id: renderSettings
                    activeFrameGraph: ForwardRenderer {
                        camera: camera
                        clearColor: "transparent"
                        showDebugOverlay: true
                    }
                },
                InputSettings { }
            ]

            // Entity {
            //     enabled: true
            //     components: [
            //         DirectionalLight {
            //             worldDirection: Qt.vector3d(0, 0, -1)
            //             intensity: 0.5
            //             color: "#ffffff"
            //         },
            //         SpotLight {
            //             intensity: 1
            //             color: "#ffffff"
            //         }
            //     ]
            // }


            PhongMaterial {
                id: material
            }

            TorusMesh {
                id: torusMesh
                radius: 5
                minorRadius: 1
                rings: 100
                slices: 20
            }

            Transform {
                id: torusTransform
                scale3D: Qt.vector3d(1.5, 1, 0.5)
                rotation: fromAxisAndAngle(Qt.vector3d(1, 0, 0), 45)
            }

            Entity {
                id: torusEntity
                components: [ torusMesh, material, torusTransform ]
            }

            SphereMesh {
                id: sphereMesh
                radius: 3
            }


            Transform {
                id: sphereTransform
                property real userAngle: 0.0
                matrix: {
                    var m = Qt.matrix4x4();
                    m.rotate(userAngle, Qt.vector3d(0, 1, 0))
                    m.translate(Qt.vector3d(20, 0, 0));
                    return m;
                }
            }

            Entity {
                id: sphereEntity
                components: [ sphereMesh, material, sphereTransform ]
            }

            NodeInstantiator {
                model: 1000  // 生成 n 个球体，用于压力测试
                delegate: Entity {
                    components: [
                        Transform {
                            translation: Qt.vector3d(
                                             Math.random() * 20 - 10,  // x (-10 ~ 10)
                                             Math.random() * 20 - 10,  // y (-10 ~ 10)
                                             Math.random() * 20 - 10   // z (-10 ~ 10)
                                             )
                        },
                        SphereMesh {
                            radius: 1
                            rings: 100  // 降低精度提高性能
                            slices: 100
                        },
                        PhongMaterial {
                            diffuse: Qt.rgba(Math.random(), Math.random(), Math.random(), 1.0)
                        }
                    ]
                }
            }
        }

    }

    Button{
        anchors.centerIn: parent
        text: qsTr("--------")
        onClicked: {
        }
    }

}
