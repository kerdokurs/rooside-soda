package main

import (
	"context"
	"fmt"
	"strconv"
	"strings"
	"time"

	"firebase.google.com/go/v4"
	"firebase.google.com/go/v4/db"
	mqtt "github.com/eclipse/paho.mqtt.golang"
)

var messagePubHandler mqtt.MessageHandler = func(client mqtt.Client, msg mqtt.Message) {
    fmt.Printf("Received message: %s from topic: %s\n", msg.Payload(), msg.Topic())
}

var connectHandler mqtt.OnConnectHandler = func(client mqtt.Client) {
    fmt.Println("Connected")
}

var connectLostHandler mqtt.ConnectionLostHandler = func(client mqtt.Client, err error) {
    fmt.Printf("Connect lost: %v", err)
}

// -1 if no button
// 0 if button 0
// 1 if button 1
var currentButtonPressed = -1
const debounceTime = 15 * time.Second
var lastTimePressed = time.Now().Add(-debounceTime)

var app *firebase.App
var database *db.Client

func setButtonPressed(button int) {
    ref := database.NewRef("rooside_soda/button/pressed/" + strconv.Itoa(button))
    ref.Set(context.Background(), true)

    time.Sleep(debounceTime)
    setButtonReleased(button)
}

func setButtonReleased(button int) {
    ref := database.NewRef("rooside_soda/button/pressed/" + strconv.Itoa(button))
    ref.Set(context.Background(), false)
}

func main() {
    var broker = "34.88.20.179"
    var port = 1883
    opts := mqtt.NewClientOptions()
    opts.AddBroker(fmt.Sprintf("mqtt://%s:%d", broker, port))
    opts.SetClientID("mqtt-proxy")
    opts.SetUsername("test")
    opts.SetPassword("test")
    opts.SetDefaultPublishHandler(messagePubHandler)
    opts.OnConnect = connectHandler
    opts.OnConnectionLost = connectLostHandler
    client := mqtt.NewClient(opts)
    token := client.Connect()
    token.Wait()
    fmt.Println("Connected to ", broker)
    if token.Error() != nil {
        fmt.Println(token.Error())
    }

    var err error
    app, err = firebase.NewApp(context.Background(), nil)
    if err != nil {
        fmt.Println("Error initializing firebase app", err)
        return
    }

    database, err = app.DatabaseWithURL(context.Background(), "https://kerdo-me.firebaseio.com")
    if err != nil {
        fmt.Println("Error initializing firebase database", err)
        return
    }

    token = client.Subscribe("button/pressed/#", 0, func(c mqtt.Client, m mqtt.Message) {
        parts := strings.Split(m.Topic(), "/")
        buttonId, err := strconv.Atoi(parts[len(parts)-1])
        if err != nil {
            fmt.Println("Error parsing button id")
            return
        }

        currentTime := time.Now()
        if currentTime.Sub(lastTimePressed) < debounceTime {
            return
        }

        go setButtonPressed(buttonId)

        fmt.Println("Button pressed: ", buttonId)
        lastTimePressed = currentTime
        currentButtonPressed = buttonId
    })
    token.Wait()

    for {
        time.Sleep(100 * time.Millisecond)
    }
}

